//
// Created by wlr on 2/19/25.
//
#include "opto/sonserializer.hpp"

#include <algorithm>
#include <bits/ctype_base.h>

SonSerializer::SonSerializer(Compile* compile,const char* file_name){
	_output = new (mtCompiler) fileStream(file_name,"w");
	_root = (Node*)compile->root();
  this->set_compile((compile));
  walk_nodes(_root);
}

SonSerializer:: ~SonSerializer()
{
	if (_root) delete _root;
	if(_output) delete _output;
	
}

void SonSerializer::walk_nodes(Node* start) {
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(start);

  while (nodeStack.length() > 0) {
    Node* n = nodeStack.pop();
    if (visited.test_set(n->_idx))
      continue;

    ++_nodeNum;
    _maxNodeIdx=n->_idx>_maxNodeIdx?n->_idx:_maxNodeIdx;
  //  for (DUIterator i = n->outs(); n->has_out(i); i++)
    //    nodeStack.push(n->out(i));
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));
    for (size_t i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        ++_edgeNum;//only count for the outgoing edges.
      }
  }
}


void SonSerializer::set_csr() {
  _isCSR=true;
  _graph=new CSRGraph(_root,_nodeNum,_edgeNum,_maxNodeIdx,C);
}

void SonSerializer::compress_and_dump() {

  _graph->compress_and_dump();
}


//______________________________________________Graph Storage_________________________________________________
CSRGraph::CSRGraph(Node* nd,uint nodeNumber,uint edgeNumber,uint maxNodeIdx,Compile* C)
  :_nodeNumber(nodeNumber),_edgeNumber(edgeNumber),_maxNodeIdx(maxNodeIdx),C(C)
{
  Node* start=nd;
  _oriOffset=(int*)C->comp_arena()->Amalloc(sizeof(int)*(1+_maxNodeIdx));
  memset(_oriOffset,-1,sizeof(int)*(1+_maxNodeIdx));
  _edge=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNumber);

  int pstart=0;
  int pend=0;
//create the csr format.
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(start);

  while (nodeStack.length() > 0) {
    Node* n = nodeStack.pop();
    if (visited.test_set(n->_idx))
      continue;

   // for (DUIterator i = n->outs(); n->has_out(i); i++)
   //     nodeStack.push(n->out(i));
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));
    for (uint i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        _edge[pend++]=n->in(i)->_idx;
      }
    _oriOffset[n->_idx]=pstart;
    pstart=pend;
  }

}

void CSRGraph::compress_and_dump() {
  reassign_idx();
  kbit_encoding();
  kbit_decoding();
  recover_idx();
}



//return i, _oriOffset[i] is the lowest upper bound of num.
//_oriOffset[i] cannot be equal to num.
int CSRGraph::find_lowest_upper_bound(const int num,const bool equal) const {
  int low=INT_MAX,idx=-1;
  for (uint i=0;i<=_maxNodeIdx;++i)
    if (((equal&&_oriOffset[i]>=num)||(!equal&&_oriOffset[i]>num))&&_oriOffset[i]<low) {
      low=_oriOffset[i];
      idx=static_cast<int>(i);
    }
  return idx;
}
int CSRGraph::lookup_idx_hash(const int old) const {
  for (uint i=0;i<_nodeNumber;++i)
    if (old==_idxHash[i])
      return i;
  return -1;
}


//The first step, only ensure the _oriOffset[i+1]-_oriOffset[i] is the outEdgeNum of node i.
//To look up the very rudimentary hash table, new->old O(1), old->new O(n)
//Restore:to be written...
//The hash table Can be further optimize by compressing bit.(after ask how many num son can reach...

void CSRGraph::reassign_idx(){
  //construct _newOffset and _idxHash, O(n^2)
  _idxHash=(int*)C->comp_arena()->Amalloc(sizeof(int)*_nodeNumber);
  _newOffset=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNumber);
  _idxHash[0]=find_lowest_upper_bound(0,1);
  _newOffset[0]=0;

  for (uint p=1;p<_nodeNumber;++p) {
    _idxHash[p]=find_lowest_upper_bound(_oriOffset[_idxHash[p-1]],0);
    _newOffset[p]=_oriOffset[_idxHash[p]];
  }

  //modify the edge to replace the old indices with the newly-assigned indices, O(n^2)
  //the newEdge can be deleted after verification.

  _newEdge=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNumber);
  for (uint i=0;i<_edgeNumber;++i)
    _newEdge[i]=lookup_idx_hash(_edge[i]);

}
//for now, just to be used to validate the correctness, compared with _oriOffset and _edge.
void CSRGraph::recover_idx() {
  bool good=true;
  //validate edge
  for (uint i=0;i<_edgeNumber;++i)
    if (_edge[i]!=_idxHash[_newEdge[i]]) {
      good=false;
      break;
    }
  //validate the idx
  //how to get _oriOffset with _newOffset and _idxHash?
  for (uint i=0;i<_nodeNumber;++i)
    if (_oriOffset[_idxHash[i]]!=_newOffset[i]) {
      good =false;
      break;
    }


  if (!good) {

    outputStream* _output = new (mtCompiler) fileStream("validate.txt","w");
    _output->print_cr("incorrect!!!!");
  }
}

void CSRGraph::set_bit(u_int8_t *obj, const int bit) {
  const u_int8_t mask=1<<bit;
  *obj=*obj|mask;
}

void CSRGraph::kbit_encoding(){
  int pBytes=0;//print to the new offset
  for (uint i=0;i<_nodeNumber;++i) {//i is the current node index.
    uint start=_newOffset[i];
    uint end=i==_nodeNumber-1?_edgeNumber:_newOffset[i+1];

    _newOffset[i]=pBytes;
    for (uint j=start;j<end;++j) {
      int obj=_newEdge[j];
      obj=obj-i;

      bool neg=obj<0;
      if (neg) {
        obj=~obj+1;//complement->source
      }
      int shift=0;

      for (int b=0;b<4;++b,++pBytes){
        u_int8_t* cur=(u_int8_t*)_newEdge+pBytes;
        if (b==0) {
          *cur=obj&0x3f;
          if (neg) set_bit(cur,6);
          shift+=6;
        }
        else {
          if ((obj&(0x7f<<shift))!=0) {
            set_bit(cur-1,7);
            *cur=obj&0x7f;
            shift+=7;
          }
          else break;
        }
      }

    }

  }
  _kbitBytesLen=pBytes;
}
void CSRGraph::kbit_decoding() {//in-place recover
  int *tmpEdge = (int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNumber);
  uint pInt=0;
  for (uint i=0;i< _nodeNumber;++i) {//for each node
    uint pstart=_newOffset[i];//byte pointer
    uint pend=i==_nodeNumber-1?_kbitBytesLen:_newOffset[i+1];
    //update the _newOffset
    _newOffset[i]=pInt;
    uint j=pstart;
    while(j<pend)//for each node's outgoing edges
    {
      int obj=0;
      int shift=0;
      bool neg=false;
      bool isFirstByte=true;
      u_int8_t * cur=(u_int8_t*)_newEdge+j;
      while(1){//decoding an integer idx.for each byte.
        if(isFirstByte){
          neg=(*cur&0x40)!=0;
          obj+=*cur&0x3f;
        }
        else obj+=(*cur&0x7f)<<shift;
        ++j;
        if((*cur&0x80)==0) break;
        shift+=isFirstByte?6:7;
        isFirstByte=false;
      }
      tmpEdge[pInt++]=neg?(i-obj):(i+obj);
    }
  }

  memcpy(_newEdge,tmpEdge,sizeof(int)*_edgeNumber);
  C->comp_arena()->Afree(tmpEdge,sizeof(int)*_edgeNumber);

}

