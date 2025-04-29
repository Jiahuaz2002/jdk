//
// Created by wlr on 2/19/25.
//
#include "opto/sonserializer.hpp"

#include <algorithm>
#include <bits/ctype_base.h>

#include "runtime/globals_extension.hpp"

SonSerializer::SonSerializer(Compile* compile,const char* file_name){
	_output = new (mtCompiler) fileStream(file_name,"w");
	_root = (Node*)compile->root();
  this->set_compile((compile));
  walk_nodes(_root);
}

SonSerializer:: ~SonSerializer()
{
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
  _graph=new CSRGraph(_root,_nodeNum,_edgeNum,C);
}

void SonSerializer::compress_and_dump() {

  _graph->compress_and_dump();
}


bool SonSerializer::deserialize() {
  return _graph->deserialize();

}



//______________________________________________Graph Storage_________________________________________________
CSRGraph::CSRGraph(Node* nd,uint nodeNumber,uint edgeNumber,Compile* C)
  :_root(nd),_nodeNum(nodeNumber),_edgeNum(edgeNumber),C(C)
{

  Node* start=nd;
  initialize_nodebyte();
  set_vptr_table(_root);
  fileStream* f= new (mtCompiler) fileStream("node.txt","w");
  f->write((char*)&_vptrNum,2);//use a space to divide vt pointers and nodes
  for (ushort i=0;i<512;++i) {
    if (_vptrTable[i]!=nullptr) {
      f->write((char*)&i,2);
      f->write((char*)&_vptrTable[i],8);
    }
  }

  _oriEdge=NEW_C_HEAP_ARRAY(int,_edgeNum<<1,mtCompiler);

  //the slot index. The actual size of it (_edgeIdxSize) may smaller than _edgeNum.
  _edgeIdx=NEW_C_HEAP_ARRAY(int, _edgeNum,mtCompiler);
  //indicating if the idx is stored or not
  //if _edgeIdxMask->get(node_oriId)=true then it mean the index of node_oriId is stored.
  //you have to transfer the bitmask from store the oriId to newly-assigned Id for deserialization
  _edgeIdxMask=new Bitmask(C,_nodeNum);

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


    store_node(n,f);//write the node to file
//preminary:no need to store anything
    //not preminary&&filled<empty need to store idx
    //not preminary&&filled>empty need to store -1
    if (preliminary_known_node(n)==false) {
      uint empty=0;
      uint filled=0;
      for (uint i = 0; i < n->len(); i++)
        if (n->in(i) != nullptr) ++filled;
        else ++empty;
      if (empty>filled)
        _edgeIdxMask->set(offsetP);
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
      else if (!preliminary_known_node(n)&&!_edgeIdxMask->get(offsetP)) {//set the empty slot as -1
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
  f->close();

  _edge=NEW_C_HEAP_ARRAY(int,_edgeNum,mtCompiler);
  for (uint i=0;i<_edgeNum;++i)
    _edge[i]=lookup_idx_hash(_oriEdge[i]);

  FREE_C_HEAP_ARRAY(int,_oriEdge);

}
CSRGraph::~CSRGraph() {
  if (_edgeIdx) FREE_C_HEAP_ARRAY(int,_edgeIdx);
  if (_offset) FREE_C_HEAP_ARRAY(int,_offset);
  if (_edge) FREE_C_HEAP_ARRAY(int,_edge);
  if (_idHash) FREE_C_HEAP_ARRAY(int,_edge);
}


void CSRGraph::compress_and_dump() {


}

bool CSRGraph::deserialize() {
//the input is _offset and _edge
//the bitmask? how should it work
  //first create a graph.start from zero.
  fileStream* f=new(mtCompiler) fileStream("node.txt","r");

  Node* root=nullptr;
  //construct(root,0,f);
  GrowableArray<Node *> *nodeSet=new GrowableArray<Node *>(_nodeNum,_nodeNum,nullptr);
  ushort opcode=0;
  f->read((void*)&_vptrNum,sizeof(char),2);
  for (int i=0;i<_vptrNum;++i) {
    f->read((void*)&opcode,sizeof(char),2);
    f->read((void*)&(_vptrTable[opcode]),sizeof(char),8);
  }

  for (uint i=0;i<_nodeNum;++i) {
    opcode=0;
    f->read((void*)&opcode,sizeof(char),2);
    Node *node=nullptr;
    void* mem=nullptr;
    //create node
    uint sz=*(_nodeBytes->get(static_cast<uint>(opcode)));
    mem=Node::operator new(sz);
    node=static_cast<Node*>(mem);
    load_node(node,f,sz,opcode);
    if (opcode==Op_Con)  C->set_cached_top_node(node);
    nodeSet->at(i)=node;
    //clear input and output array
    Node*** in_tmp=reinterpret_cast<Node***>((char*)node+8);//this, point to the original value,you have to modify the value of _in that is *(&_in)
    Node*** out_tmp= reinterpret_cast<Node***>((char*)node+16) ;
    *in_tmp=//the offset of _in is 8,of _out is 16,of _outcnt(4bytes) is 32
      (Node **) ((char *) (C->node_arena()->AmallocWords( node->len()* sizeof(void*))));
    memset(*in_tmp, 0, node->len() * sizeof(Node*));
    if (node->is_top()==false) *out_tmp=NO_OUT_ARRAY;
    *reinterpret_cast<int*>((char*)node+32)=0;//clear _outcnt
    *reinterpret_cast<int*>((char*)node+36)=0;//clear _outmax
  }
  f->close();
  uint pIdx=0;//index the edgeIdx
  for (uint i=0;i<_nodeNum;++i) {
    //first get the sizeof the input(maybe contain -1
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    if(_edgeIdxMask->get(i)) {//the idx is stored, case 1 not preliminary but filled<empty

      uint pEdge=_offset[i];//indexes the edge array
      for (uint j=pIdx;j<pIdx+sz;++j) {
        if (_edgeIdx[j]<static_cast<int>(nodeSet->at(i)->req()))
            nodeSet->at(i)->init_req(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
        else
          nodeSet->at(i)->set_prec(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
      }
      pIdx+=sz;
    }
    else {//nooooooo, the idx is not stored, so you have to analysis.
      //two cases:
      //case 0:preliminary,add,sub...
      //case 2:-1.
      if (preliminary_known_node(nodeSet->at(i))) {//add sub...
        uint idx=0;
        if (sz==3)
          nodeSet->at(i)->init_req(idx,nodeSet->at(_edge[pEdge++]));
        nodeSet->at(i)->init_req(idx+1,nodeSet->at(_edge[pEdge]));
        nodeSet->at(i)->init_req(idx+2,nodeSet->at(_edge[pEdge+1]));
      }
      else {//-1.
        for (uint j=0;j<sz;++j)
          if (_edge[j+pEdge]!=-1) {
            if (j<nodeSet->at(i)->req())
              nodeSet->at(i)->init_req(j,nodeSet->at(_edge[pEdge+j]));
            else
              nodeSet->at(i)->set_prec(j,nodeSet->at(_edge[pEdge+j]));
          }
      }
    }
  }


  C->set_root(static_cast<RootNode*>(nodeSet->at(0)));
  return 0;

}



void CSRGraph::store_node(Node* n,fileStream* f){
  ushort opcode=n->Opcode();
  f->write((char*)&opcode,2);
  int sz=*(_nodeBytes->get(opcode));
//vt pointer, offset 0
  f->write((char*)n+8,sz-8);
}

void CSRGraph::load_node(Node*n, fileStream*f,int sz,ushort op) {
  *(void**)n=_vptrTable[op];
  f->read((char*)n+8,sizeof(char),sz-8);
}


void CSRGraph::set_vptr_table(Node *root) {
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(root);
  while (nodeStack.length() > 0) {
    Node* n = nodeStack.pop();
    if (visited.test_set(n->_idx))
      continue;
    if (_vptrTable[n->Opcode()]==nullptr) {
      _vptrTable[n->Opcode()]=*(void**)n;
      ++_vptrNum;
    }
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));
    for (size_t i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
      }
  }
}


//there should be another case: that is compact and the only thing we need to know is the node's input size(to be done
bool CSRGraph::preliminary_known_node(Node *node) {
    switch (node->Opcode()) {
      case Op_AddI: case Op_AddL: case Op_AddF: case Op_AddD:
      case Op_MulI: case Op_MulL: case Op_MulF: case Op_MulD:
      case Op_AndI: case Op_AndL:
      case Op_OrI: case Op_OrL:
      case Op_XorI: case Op_XorL:
      case Op_SubI: case Op_SubL:
      case Op_DivI: case Op_DivL: case Op_DivF: case Op_DivD:
          return true;
      default:return false;
    }
}

int CSRGraph::lookup_idx_hash(const int old) const {
  if (old==-1) return -1;
  for (uint i=0;i<_nodeNum;++i)
    if (old==_idHash[i])
      return i;
  return -1;
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


//______________________________________________Auxiliary Class_________________________________________________
