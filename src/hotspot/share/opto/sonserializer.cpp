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
  _graph=new CSRGraph(_root,_nodeNum,_edgeNum,_maxNodeIdx,C);
}

void SonSerializer::compress_and_dump() {

  _graph->compress_and_dump();
}


bool SonSerializer::deserialize() {
  return _graph->deserialize();
}



//______________________________________________Graph Storage_________________________________________________
CSRGraph::CSRGraph(Node* nd,uint nodeNumber,uint edgeNumber,uint maxNodeIdx,Compile* C)
  :_root(nd),_nodeNum(nodeNumber),_edgeNum(edgeNumber),_maxNodeIdx(maxNodeIdx),C(C)
{

  Node* start=nd;
  initialize_nodebyte();
  fileStream* outputNode= new (mtCompiler) fileStream("node.txt","w");
  _oriOffset=NEW_C_HEAP_ARRAY(int,1+_maxNodeIdx,mtCompiler);
  memset(_oriOffset,-1,sizeof(int)*(1+_maxNodeIdx));
  _oriEdge=NEW_C_HEAP_ARRAY(int,_edgeNum*2,mtCompiler);

  //the slot index. The actual size of it (_edgeIdxSize) may smaller than _edgeNum.
  _edgeIdx=NEW_C_HEAP_ARRAY(int, _edgeNum,mtCompiler);
  //indicating if the idx is stored or not
  //if _edgeIdxMask->get(node_oriId)=true then it mean the index of node_oriId is stored.
  //you have to transfer the bitmask from store the oriId to newly-assigned Id for deserialization
  _edgeIdxMask=new Bitmask(C,_maxNodeIdx);


  _idHash=NEW_C_HEAP_ARRAY(int,_nodeNum,mtCompiler);
  _offset=NEW_C_HEAP_ARRAY(int,_nodeNum,mtCompiler);

  uint pstart=0;//for edge and offset
  uint pend=0;

  uint maskp=0;//

//create the csr format.
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(start);

  int offsetP=0;//directly reassign the id here. offsetP from 0 to nodeNum
  while (nodeStack.length() > 0) {//start traversing the graph
    Node* n = nodeStack.pop();

    if (visited.test_set(n->_idx))//test and set
      continue;

    int opcode=n->Opcode();
    outputNode->write((char*)&opcode,4);
    int sz=*(_nodeBytes->get(opcode));
    outputNode->write((char*)n,sz);


    if (need_input_index(n))
      //then u can directly lookup the bitmask to know if this node(with _idx) has stored its index
      _edgeIdxMask->set(offsetP);
    //if we need to store the indices, there are two cases:
    //1.filled slots > empty slots, just set the empty slots as -1,dont store the idx
    //2.empty slots > filled slots, just to store the filled slots indices
    bool nullflag=0;//this mean have to point to the null node(set the empty slots as -1
    uint empty=0;
    uint filled=0;
    for (uint i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) ++filled;
      else ++empty;
    if (empty<filled) {
      nullflag=true;
      _edgeIdxMask->clear(offsetP);
    }

    //traverse the output edges make sure no nodes are omitted
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));

    for (uint i = 0; i < n->len(); i++) {
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        _oriEdge[pend++]=n->in(i)->_idx;
        if (_edgeIdxMask->get(offsetP))
          _edgeIdx[maskp++]=i;//_edgeIdx and the _oriEdge and _edge actually preserve the same order, dont modify them for now
      }
      else if (nullflag) {//set the empty slot as -1
        _oriEdge[pend++]=-1;
      }
    }
    _offset[offsetP]=pstart;
    _idHash[offsetP++]=n->_idx;
    pstart=pend;
  }
  _actlEdgeNum=_edgeNum;//update the edgeNum and the actlEdgeNum because of the null node(the -1 edge)
  _edgeNum=pend;//
  _edgeIdxSize=maskp;//totally how many indices are stored
  outputNode->close();

}
CSRGraph::~CSRGraph() {
 // if (_edgeIdxMask) delete _edgeIdxMask;
  if (_oriOffset) FREE_C_HEAP_ARRAY(int,_oriOffset);
  if (_oriEdge) FREE_C_HEAP_ARRAY(int,_oriEdge);
  if (_edgeIdx) FREE_C_HEAP_ARRAY(int,_edgeIdx);
  if (_offset) FREE_C_HEAP_ARRAY(int,_offset);
  if (_edge) FREE_C_HEAP_ARRAY(int,_edge);
  if (_idHash) FREE_C_HEAP_ARRAY(int,_edge);

}


void CSRGraph::compress_and_dump() {
  reassign_idx();
 // kbit_encoding();
  //kbit_decoding();
  recover_idx();

}


bool CSRGraph::deserialize() {
//the input is _offset and _edge
//the bitmask? how should it work
  //first create a graph.start from zero.
  fileStream* f=new(mtCompiler) fileStream("node.txt");
  int opcode=0;
  Node* root=nullptr;
  //construct(root,0,f);
  GrowableArray<Node *> *nodeSet=new GrowableArray<Node *>(_nodeNum);
  for (uint i=0;i<_nodeNum;++i) {
    int opcode=0;
    f->read((void*)&opcode,sizeof(int),1);
    Node *node=nullptr;
    switch (opcode) {
      case Op_Root:
        node=new RootNode();//you have to replace the _in and _out, or else they will point to original ones.
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Root)));
        break;
      case Op_Start:
        node=new StartNode(nullptr,nullptr);//need root node and a domain;
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Start)));
        break;
      case Op_Con:
        node=new ConNode(nullptr);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Con)));
        break;
      case Op_Parm:
        node=new ParmNode(nullptr,0);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Parm)));
        break;
      case Op_AddI:
        node= new AddINode(nullptr,nullptr);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_AddI)));
      case Op_Return:
        node=new ReturnNode(0,nullptr,nullptr,nullptr,nullptr,nullptr);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Return )));
    }
    nodeSet->at(i)=node;
  }
  //cover the in and out
  uint pIdx=0;//index rhe edgeIdx
  for (uint i=0;i<_nodeNum;++i) {
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];

    if (_edgeIdxMask->get(i)) {//the idx is stored, great!
      //first get the sizeof the input
      uint pEdge=_offset[i];//indexes the edge array
      for (uint j=pIdx;j<pIdx+sz;++j) {
        nodeSet->at(i)->init_req(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
      }
      pIdx+=sz;
    }
    else {//nooooooo, the idx is not stored, so you have to analysis.
      //two cases:
      //1.add,sub...
      //2.-1.
      if (need_input_index(nodeSet->at(i))==false) {//add sub...
        if (sz==3)
          nodeSet->at(i)->init_req(0,nodeSet->at(_edge[pEdge++]));
        nodeSet->at(i)->init_req(0,nodeSet->at(_edge[pEdge]));
        nodeSet->at(i)->init_req(0,nodeSet->at(_edge[pEdge+1]));
      }
      else {//-1.
        for (uint j=0;j<sz;++j)
          if (_edge[j+pEdge]!=-1)
          nodeSet->at(i)->init_req(j,nodeSet->at(_edge[pEdge+j]));
      }
    }
  }


  C->set_root(static_cast<RootNode*>(nodeSet->at(0)));
  return 0;

}

bool CSRGraph::need_input_index(Node *node) {
    switch (node->Opcode()) {
      case Op_AddI: case Op_AddL: case Op_AddF: case Op_AddD:case Op_AddP:
      case Op_MulI: case Op_MulL: case Op_MulF: case Op_MulD:
      case Op_AndI: case Op_AndL:
      case Op_OrI: case Op_OrL:
      case Op_XorI: case Op_XorL:
      case Op_SubI: case Op_SubL:
      case Op_DivI: case Op_DivL: case Op_DivF: case Op_DivD:
          return false;
      default:return true;
    }
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
  if (old==-1) return -1;
  for (uint i=0;i<_nodeNum;++i)
    if (old==_idHash[i])
      return i;
  return -1;
}

//The first step, only ensure the _oriOffset[i+1]-_oriOffset[i] is the outEdgeNum of node i.
//To look up the very rudimentary hash table, new->old O(1), old->new O(n)
//Restore:to be written...
//The hash table Can be further optimize by compressing bit.(after ask how many num son can reach...

void CSRGraph::reassign_idx(){
  //construct _offset and _idxHash, O(n^2)
  //_idHash=NEW_C_HEAP_ARRAY(int,_nodeNum,mtCompiler);
  //_offset=NEW_C_HEAP_ARRAY(int,_edgeNum,mtCompiler);

  //_idHash[0]=find_lowest_upper_bound(0,1);
  //_offset[0]=0;

  //for (uint p=1;p<_nodeNum;++p) {
  //  _idHash[p]=find_lowest_upper_bound(_oriOffset[_idHash[p-1]],0);
  //  _offset[p]=_oriOffset[_idHash[p]];
  //}
  //modify the edge to replace the old indices with the newly-assigned indices, O(n^2)
  //the _edge can be deleted after verification.
  _edge=NEW_C_HEAP_ARRAY(int,_edgeNum,mtCompiler);
  for (uint i=0;i<_edgeNum;++i)
    _edge[i]=lookup_idx_hash(_oriEdge[i]);
}
//for now, just to be used to validate the correctness, compared with _oriOffset and _edge.
void CSRGraph::recover_idx() {
  bool good=true;
  //validate edge
  for (uint i=0;i<_edgeNum;++i)
    if (_oriEdge[i]!=_idHash[_edge[i]]) {
      good=false;
      break;
    }
  //validate the idx
  //how to get _oriOffset with _offset and _idxHash?
  for (uint i=0;i<_nodeNum;++i)
    if (_oriOffset[_idHash[i]]!=_offset[i]) {
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
  for (uint i=0;i<_nodeNum;++i) {//i is the current node index.
    uint start=_offset[i];
    uint end=i==_nodeNum-1?_edgeNum:_offset[i+1];

    _offset[i]=pBytes;
    for (uint j=start;j<end;++j) {
      int obj=_edge[j];
      obj=obj-i;

      bool neg=obj<0;
      if (neg) {
        obj=~obj+1;//complement->source
      }
      int shift=0;

      for (int b=0;b<4;++b,++pBytes){
        u_int8_t* cur=(u_int8_t*)_edge+pBytes;
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
  int *tmpEdge = (int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNum);
  uint pInt=0;
  for (uint i=0;i< _nodeNum;++i) {//for each node
    uint pstart=_offset[i];//byte pointer
    uint pend=i==_nodeNum-1?_kbitBytesLen:_offset[i+1];
    //update the _offset
    _offset[i]=pInt;
    uint j=pstart;
    while(j<pend)//for each node's outgoing edges
    {
      int obj=0;
      int shift=0;
      bool neg=false;
      bool isFirstByte=true;
      u_int8_t * cur=(u_int8_t*)_edge+j;
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

  memcpy(_edge,tmpEdge,sizeof(int)*_edgeNum);
  C->comp_arena()->Afree(tmpEdge,sizeof(int)*_edgeNum);

}


void CSRGraph::initialize_nodebyte() {
  _nodeBytes=new ResourceHashtable<int, int>();
  _nodeBytes->put(Op_Root,sizeof(RootNode));
  _nodeBytes->put(Op_Con,sizeof(ConNode));
  _nodeBytes->put(Op_Start,sizeof(StartNode));
  _nodeBytes->put(Op_ConI,sizeof(ConINode));
  _nodeBytes->put(Op_Parm,sizeof(ParmNode));
  _nodeBytes->put(Op_AddI,sizeof(AddINode));
  _nodeBytes->put(Op_Return,sizeof(ReturnNode));


}


//______________________________________________Auxiliary Class_________________________________________________




