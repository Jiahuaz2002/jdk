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
  fileStream* f=new(mtCompiler) fileStream("node.txt","r");
  int opcode=0;
  Node* root=nullptr;
  //construct(root,0,f);
  GrowableArray<Node *> *nodeSet=new GrowableArray<Node *>(_nodeNum,_nodeNum,nullptr);
  for (uint i=0;i<_nodeNum;++i) {
    int opcode=0;
    f->read((void*)&opcode,sizeof(char),4);
    Node *node=nullptr;
    void* mem=nullptr;
    switch (opcode) {
      case Op_Root:
        node=new RootNode();//you have to replace the _in and _out, or else they will point to original ones.
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Root)));
        break;
      case Op_Start:
        //node=new StartNode(nullptr,nullptr);//need root node and a domain;
        mem=Node::operator new(sizeof(StartNode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Start)));
        break;
      case Op_Con:
        mem=Node::operator new(sizeof(ConNode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Con)));
        C->set_cached_top_node(node);
        break;
      case Op_ConI:
        mem=Node::operator new(sizeof(ConINode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_ConI)));
        break;
      case Op_Parm:
        mem=Node::operator new(sizeof(ParmNode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Parm)));
        break;
      case Op_AddI:
        mem=Node::operator new(sizeof(AddINode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_AddI)));
        break;
      case Op_ConL:
        mem = Node::operator new(sizeof(ConLNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_ConL)));
        break;

      case Op_Return:
        mem=Node::operator new(sizeof(ReturnNode));
        node=static_cast<Node*>(mem);
        f->read((void*)node,sizeof(char),*(_nodeBytes->get(Op_Return )));
        break;
        case Op_AndI:
        mem = Node::operator new(sizeof(AndINode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_AndI)));
        break;

    case Op_Bool:
        mem = Node::operator new(sizeof(BoolNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_Bool)));
        break;

    case Op_CmpI:
        mem = Node::operator new(sizeof(CmpINode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_CmpI)));
        break;

    case Op_CountedLoop:
        mem = Node::operator new(sizeof(CountedLoopNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_CountedLoop)));
        break;

    case Op_CountedLoopEnd:
        mem = Node::operator new(sizeof(CountedLoopEndNode));
        //CountedLoopEndNode* typed = new (mem) CountedLoopEndNode();
       node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_CountedLoopEnd)));
        break;

    case Op_If:
        mem = Node::operator new(sizeof(IfNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_If)));
        break;

    case Op_IfFalse:
        mem = Node::operator new(sizeof(IfFalseNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_IfFalse)));
        break;

    case Op_IfTrue:
        mem = Node::operator new(sizeof(IfTrueNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_IfTrue)));
        break;

    case Op_Phi:
        mem = Node::operator new(sizeof(PhiNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_Phi)));
        break;

    case Op_Region:
        mem = Node::operator new(sizeof(RegionNode));
        node = static_cast<Node*>(mem);
        f->read((void*)node, sizeof(char), *(_nodeBytes->get(Op_Region)));
        break;

    }
    nodeSet->at(i)=node;
  }
  f->close();
  //cover the in and out

  uint pIdx=0;//index the edgeIdx
  for (uint i=0;i<_nodeNum;++i) {


    // the _in and _out are pointers which means they are still point to the previous array, which is not good.

    Node*** in_tmp=reinterpret_cast<Node***>((char*)nodeSet->at(i)+8);//this, point to the original value,you have to modify the value of _in that is *(&_in)
    Node*** out_tmp= reinterpret_cast<Node***>((char*)nodeSet->at(i)+16) ;
    *in_tmp=//the offset of _in is 8,of _out is 16,of _outcnt(4bytes) is 32
      (Node **) ((char *) (C->node_arena()->AmallocWords( nodeSet->at(i)->len()* sizeof(void*))));
    memset(*in_tmp, 0, nodeSet->at(i)->len() * sizeof(Node*));
    *out_tmp=NO_OUT_ARRAY;
    //*out_tmp=
    //  (Node **) ((char *) (C->node_arena()->AmallocWords(nodeSet->at(i)->outcnt() * sizeof(void*))));
    //memset(*out_tmp, 0, nodeSet->at(i)->outcnt() * sizeof(Node*));
    *reinterpret_cast<int*>((char*)nodeSet->at(i)+32)=0;
    *reinterpret_cast<int*>((char*)nodeSet->at(i)+36)=0;
    //first get the sizeof the input(maybe contain -1
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    if(_edgeIdxMask->get(i)) {//the idx is stored, case 1 not preliminary but filled<empty

      uint pEdge=_offset[i];//indexes the edge array
      for (uint j=pIdx;j<pIdx+sz;++j) {
        nodeSet->at(i)->init_req(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
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
          if (_edge[j+pEdge]!=-1)
          nodeSet->at(i)->init_req(j,nodeSet->at(_edge[pEdge+j]));
      }
    }
  }


  C->set_root(static_cast<RootNode*>(nodeSet->at(0)));
  return 0;

}


//there should be another case: that is compact and the only thing we need to know is the node's input size(to be done
bool CSRGraph::preliminary_known_node(Node *node) {
    switch (node->Opcode()) {
      case Op_AddI: case Op_AddL: case Op_AddF: case Op_AddD:case Op_AddP:
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
  _nodeBytes->put(Op_Con, sizeof(ConNode));
  _nodeBytes->put(Op_Start,sizeof(StartNode));
  _nodeBytes->put(Op_ConI,sizeof(ConINode));
  _nodeBytes->put(Op_Parm,sizeof(ParmNode));
  _nodeBytes->put(Op_AddI,sizeof(AddINode));
  _nodeBytes->put(Op_Return,sizeof(ReturnNode));
//AbsNode
  _nodeBytes->put(Op_AbsD,    sizeof(AbsDNode));
  _nodeBytes->put(Op_AbsF,    sizeof(AbsFNode));
  _nodeBytes->put(Op_AbsI,    sizeof(AbsINode));
  _nodeBytes->put(Op_AbsL,    sizeof(AbsLNode));
  //AddNode
  // Add
  _nodeBytes->put(Op_AddD,   sizeof(AddDNode));
  _nodeBytes->put(Op_AddF,   sizeof(AddFNode));
  _nodeBytes->put(Op_AddL,   sizeof(AddLNode));

  // Max/Min
  _nodeBytes->put(Op_MaxD,   sizeof(MaxDNode));
  _nodeBytes->put(Op_MaxF,   sizeof(MaxFNode));
  _nodeBytes->put(Op_MaxI,   sizeof(MaxINode));
  _nodeBytes->put(Op_MaxL,   sizeof(MaxLNode));
  _nodeBytes->put(Op_MinD,   sizeof(MinDNode));
  _nodeBytes->put(Op_MinF,   sizeof(MinFNode));
  _nodeBytes->put(Op_MinI,   sizeof(MinINode));
  _nodeBytes->put(Op_MinL,   sizeof(MinLNode));

  // Bitwise Ops
  _nodeBytes->put(Op_OrI,    sizeof(OrINode));
  _nodeBytes->put(Op_OrL,    sizeof(OrLNode));
  _nodeBytes->put(Op_XorI,   sizeof(XorINode));
  _nodeBytes->put(Op_XorL,   sizeof(XorLNode));


  _nodeBytes->put(Op_AddP,                  sizeof(AddPNode));
  _nodeBytes->put(Op_AtanD,                 sizeof(AtanDNode));
  _nodeBytes->put(Op_Binary,                sizeof(BinaryNode));
  _nodeBytes->put(Op_Bool,                  sizeof(BoolNode));
  _nodeBytes->put(Op_BoxLock,               sizeof(BoxLockNode));
  _nodeBytes->put(Op_CacheWB,               sizeof(CacheWBNode));
  _nodeBytes->put(Op_CacheWBPostSync,       sizeof(CacheWBPostSyncNode));
  _nodeBytes->put(Op_CacheWBPreSync,        sizeof(CacheWBPreSyncNode));
  _nodeBytes->put(Op_CastP2X,               sizeof(CastP2XNode));
  _nodeBytes->put(Op_CastX2P,               sizeof(CastX2PNode));
  _nodeBytes->put(Op_ClearArray,            sizeof(ClearArrayNode));
  _nodeBytes->put(Op_CmpLTMask,             sizeof(CmpLTMaskNode));
  _nodeBytes->put(Op_Conv2B,                sizeof(Conv2BNode));
  _nodeBytes->put(Op_CopySignD,             sizeof(CopySignDNode));
  _nodeBytes->put(Op_CopySignF,             sizeof(CopySignFNode));

  // CountBitsNode family
  //_nodeBytes->put(Op_CountLeadingZerosI,    sizeof(CountLeadingZerosINode));
  _nodeBytes->put(Op_CountLeadingZerosL,    sizeof(CountLeadingZerosLNode));
  _nodeBytes->put(Op_CountTrailingZerosI,   sizeof(CountTrailingZerosINode));
  _nodeBytes->put(Op_CountTrailingZerosL,   sizeof(CountTrailingZerosLNode));
  _nodeBytes->put(Op_PopCountI,             sizeof(PopCountINode));
  _nodeBytes->put(Op_PopCountL,             sizeof(PopCountLNode));
  // Basic nodes
  _nodeBytes->put(Op_Digit,                sizeof(DigitNode));
  _nodeBytes->put(Op_DivD,                 sizeof(DivDNode));
  _nodeBytes->put(Op_DivF,                 sizeof(DivFNode));
  _nodeBytes->put(Op_DivI,                 sizeof(DivINode));
  _nodeBytes->put(Op_DivL,                 sizeof(DivLNode));
  _nodeBytes->put(Op_EncodeISOArray,       sizeof(EncodeISOArrayNode));

  // ExtractNode family
  _nodeBytes->put(Op_ExtractB,             sizeof(ExtractBNode));
  _nodeBytes->put(Op_ExtractC,             sizeof(ExtractCNode));
  _nodeBytes->put(Op_ExtractD,             sizeof(ExtractDNode));
  _nodeBytes->put(Op_ExtractF,             sizeof(ExtractFNode));
  _nodeBytes->put(Op_ExtractI,             sizeof(ExtractINode));
  _nodeBytes->put(Op_ExtractL,             sizeof(ExtractLNode));
  _nodeBytes->put(Op_ExtractS,             sizeof(ExtractSNode));
  _nodeBytes->put(Op_ExtractUB,            sizeof(ExtractUBNode));

  // FmaNode family;
  _nodeBytes->put(Op_FmaD,                 sizeof(FmaDNode));
  _nodeBytes->put(Op_FmaF,                 sizeof(FmaFNode));

  // Control / utility
  _nodeBytes->put(Op_Goto,                 sizeof(GotoNode));
  _nodeBytes->put(Op_Halt,                 sizeof(HaltNode));

  // Floating-point checks
  _nodeBytes->put(Op_IsFiniteD,            sizeof(IsFiniteDNode));
  _nodeBytes->put(Op_IsFiniteF,            sizeof(IsFiniteFNode));
  _nodeBytes->put(Op_IsInfiniteD,          sizeof(IsInfiniteDNode));
  _nodeBytes->put(Op_IsInfiniteF,          sizeof(IsInfiniteFNode));
  // Load/Store family

  // CompareAndExchangeNode family
  _nodeBytes->put(Op_CompareAndExchangeB,                 sizeof(CompareAndExchangeBNode));
  _nodeBytes->put(Op_CompareAndExchangeI,                 sizeof(CompareAndExchangeINode));
  _nodeBytes->put(Op_CompareAndExchangeL,                 sizeof(CompareAndExchangeLNode));
  _nodeBytes->put(Op_CompareAndExchangeS,                 sizeof(CompareAndExchangeSNode));

  _nodeBytes->put(Op_CompareAndExchangeN,                 sizeof(CompareAndExchangeNNode));
  _nodeBytes->put(Op_ShenandoahCompareAndExchangeN,       sizeof(ShenandoahCompareAndExchangeNNode));

  _nodeBytes->put(Op_CompareAndExchangeP,                 sizeof(CompareAndExchangePNode));
  _nodeBytes->put(Op_ShenandoahCompareAndExchangeP,       sizeof(ShenandoahCompareAndExchangePNode));

  // GetAndAdd family
  _nodeBytes->put(Op_GetAndAddB,                          sizeof(GetAndAddBNode));
  _nodeBytes->put(Op_GetAndAddI,                          sizeof(GetAndAddINode));
  _nodeBytes->put(Op_GetAndAddL,                          sizeof(GetAndAddLNode));
  _nodeBytes->put(Op_GetAndAddS,                          sizeof(GetAndAddSNode));

  // GetAndSet family
  _nodeBytes->put(Op_GetAndSetB,                          sizeof(GetAndSetBNode));
  _nodeBytes->put(Op_GetAndSetI,                          sizeof(GetAndSetINode));
  _nodeBytes->put(Op_GetAndSetL,                          sizeof(GetAndSetLNode));
  _nodeBytes->put(Op_GetAndSetN,                          sizeof(GetAndSetNNode));
  _nodeBytes->put(Op_GetAndSetP,                          sizeof(GetAndSetPNode));
  _nodeBytes->put(Op_GetAndSetS,                          sizeof(GetAndSetSNode));

  // CompareAndSwap family
  _nodeBytes->put(Op_CompareAndSwapB,                      sizeof(CompareAndSwapBNode));
  _nodeBytes->put(Op_CompareAndSwapI,                      sizeof(CompareAndSwapINode));
  _nodeBytes->put(Op_CompareAndSwapL,                      sizeof(CompareAndSwapLNode));
  _nodeBytes->put(Op_CompareAndSwapN,                      sizeof(CompareAndSwapNNode));
  _nodeBytes->put(Op_ShenandoahCompareAndSwapN,            sizeof(ShenandoahCompareAndSwapNNode));
  _nodeBytes->put(Op_CompareAndSwapP,                      sizeof(CompareAndSwapPNode));
  _nodeBytes->put(Op_ShenandoahCompareAndSwapP,            sizeof(ShenandoahCompareAndSwapPNode));
  _nodeBytes->put(Op_CompareAndSwapS,                      sizeof(CompareAndSwapSNode));

  // WeakCompareAndSwap family
  _nodeBytes->put(Op_WeakCompareAndSwapB,                  sizeof(WeakCompareAndSwapBNode));
  _nodeBytes->put(Op_WeakCompareAndSwapI,                  sizeof(WeakCompareAndSwapINode));
  _nodeBytes->put(Op_WeakCompareAndSwapL,                  sizeof(WeakCompareAndSwapLNode));
  _nodeBytes->put(Op_WeakCompareAndSwapN,                  sizeof(WeakCompareAndSwapNNode));
  _nodeBytes->put(Op_ShenandoahWeakCompareAndSwapN,        sizeof(ShenandoahWeakCompareAndSwapNNode));
  _nodeBytes->put(Op_WeakCompareAndSwapP,                  sizeof(WeakCompareAndSwapPNode));
  _nodeBytes->put(Op_ShenandoahWeakCompareAndSwapP,        sizeof(ShenandoahWeakCompareAndSwapPNode));
  _nodeBytes->put(Op_WeakCompareAndSwapS,                  sizeof(WeakCompareAndSwapSNode));

  // Other related nodes

  _nodeBytes->put(Op_LoopLimit,                            sizeof(LoopLimitNode));
  _nodeBytes->put(Op_LowerCase,                            sizeof(LowerCaseNode));

  // LShift family
  _nodeBytes->put(Op_LShiftI,                              sizeof(LShiftINode));
  _nodeBytes->put(Op_LShiftL,                              sizeof(LShiftLNode));
// MachNode family//is this necessary?

_nodeBytes->put(Op_MergeMem, sizeof(MergeMemNode));
_nodeBytes->put(Op_ModD, sizeof(ModDNode));
_nodeBytes->put(Op_ModF, sizeof(ModFNode));
_nodeBytes->put(Op_ModI, sizeof(ModINode));
_nodeBytes->put(Op_ModL, sizeof(ModLNode));

_nodeBytes->put(Op_MoveD2L, sizeof(MoveD2LNode));
_nodeBytes->put(Op_MoveF2I, sizeof(MoveF2INode));
_nodeBytes->put(Op_MoveI2F, sizeof(MoveI2FNode));
_nodeBytes->put(Op_MoveL2D, sizeof(MoveL2DNode));
_nodeBytes->put(Op_MulAddS2I, sizeof(MulAddS2INode));
_nodeBytes->put(Op_MulHiL, sizeof(MulHiLNode));
_nodeBytes->put(Op_MulD, sizeof(MulDNode));
_nodeBytes->put(Op_MulF, sizeof(MulFNode));
_nodeBytes->put(Op_MulI, sizeof(MulINode));
_nodeBytes->put(Op_MulL, sizeof(MulLNode));
_nodeBytes->put(Op_Multi, sizeof(MultiNode));
_nodeBytes->put(Op_Blackhole, sizeof(BlackholeNode));
_nodeBytes->put(Op_DivMod, sizeof(DivModNode));

_nodeBytes->put(Op_SafePoint, sizeof(SafePointNode));
_nodeBytes->put(Op_Start, sizeof(StartNode));

_nodeBytes->put(Op_NegD, sizeof(NegDNode));
_nodeBytes->put(Op_NegF, sizeof(NegFNode));
_nodeBytes->put(Op_NegI, sizeof(NegINode));
_nodeBytes->put(Op_NegL, sizeof(NegLNode));
_nodeBytes->put(Op_Opaque1, sizeof(Opaque1Node));
_nodeBytes->put(Op_OpaqueLoopInit, sizeof(OpaqueLoopInitNode));
_nodeBytes->put(Op_OpaqueLoopStride, sizeof(OpaqueLoopStrideNode));
_nodeBytes->put(Op_OpaqueZeroTripGuard, sizeof(OpaqueZeroTripGuardNode));
_nodeBytes->put(Op_OpaqueInitializedAssertionPredicate, sizeof(OpaqueInitializedAssertionPredicateNode));
_nodeBytes->put(Op_OpaqueNotNull, sizeof(OpaqueNotNullNode));
_nodeBytes->put(Op_OpaqueTemplateAssertionPredicate, sizeof(OpaqueTemplateAssertionPredicateNode));
_nodeBytes->put(Op_PartialSubtypeCheck, sizeof(PartialSubtypeCheckNode));
_nodeBytes->put(Op_PrefetchAllocation, sizeof(PrefetchAllocationNode));
_nodeBytes->put(Op_ProfileBoolean, sizeof(ProfileBooleanNode));
_nodeBytes->put(Op_Proj, sizeof(ProjNode));
_nodeBytes->put(Op_CProj, sizeof(CProjNode));
_nodeBytes->put(Op_JProj, sizeof(JProjNode));
_nodeBytes->put(Op_MachProj, sizeof(MachProjNode));
_nodeBytes->put(Op_Parm, sizeof(ParmNode));
_nodeBytes->put(Op_SCMemProj, sizeof(SCMemProjNode));

_nodeBytes->put(Op_AddReductionVD, sizeof(AddReductionVDNode));
_nodeBytes->put(Op_AddReductionVF, sizeof(AddReductionVFNode));
_nodeBytes->put(Op_AddReductionVI, sizeof(AddReductionVINode));
_nodeBytes->put(Op_AddReductionVL, sizeof(AddReductionVLNode));
_nodeBytes->put(Op_AndReductionV, sizeof(AndReductionVNode));
_nodeBytes->put(Op_MaxReductionV, sizeof(MaxReductionVNode));
_nodeBytes->put(Op_MinReductionV, sizeof(MinReductionVNode));
_nodeBytes->put(Op_MulReductionVD, sizeof(MulReductionVDNode));
_nodeBytes->put(Op_MulReductionVF, sizeof(MulReductionVFNode));
_nodeBytes->put(Op_MulReductionVI, sizeof(MulReductionVINode));
_nodeBytes->put(Op_MulReductionVL, sizeof(MulReductionVLNode));
_nodeBytes->put(Op_OrReductionV, sizeof(OrReductionVNode));
_nodeBytes->put(Op_XorReductionV, sizeof(XorReductionVNode));
_nodeBytes->put(Op_Region, sizeof(RegionNode));
_nodeBytes->put(Op_Loop, sizeof(LoopNode));
_nodeBytes->put(Op_Rethrow, sizeof(RethrowNode));
_nodeBytes->put(Op_Return, sizeof(ReturnNode));
_nodeBytes->put(Op_ForwardException, sizeof(ForwardExceptionNode));
_nodeBytes->put(Op_TailCall, sizeof(TailCallNode));
_nodeBytes->put(Op_TailJump, sizeof(TailJumpNode));
_nodeBytes->put(Op_ReverseBytesI, sizeof(ReverseBytesINode));
_nodeBytes->put(Op_ReverseBytesL, sizeof(ReverseBytesLNode));
_nodeBytes->put(Op_ReverseBytesS, sizeof(ReverseBytesSNode));
_nodeBytes->put(Op_ReverseBytesUS, sizeof(ReverseBytesUSNode));
_nodeBytes->put(Op_ReverseI, sizeof(ReverseINode));
_nodeBytes->put(Op_ReverseL, sizeof(ReverseLNode));
_nodeBytes->put(Op_RoundD, sizeof(RoundDNode));
_nodeBytes->put(Op_RoundDoubleMode, sizeof(RoundDoubleModeNode));
_nodeBytes->put(Op_RoundDouble, sizeof(RoundDoubleNode));
_nodeBytes->put(Op_RoundFloat, sizeof(RoundFloatNode));
_nodeBytes->put(Op_RoundF, sizeof(RoundFNode));
_nodeBytes->put(Op_RShiftI, sizeof(RShiftINode));
_nodeBytes->put(Op_RShiftL, sizeof(RShiftLNode));
_nodeBytes->put(Op_ShenandoahLoadReferenceBarrier, sizeof(ShenandoahLoadReferenceBarrierNode));
_nodeBytes->put(Op_SignumD, sizeof(SignumDNode));
_nodeBytes->put(Op_SignumF, sizeof(SignumFNode));
_nodeBytes->put(Op_SqrtD, sizeof(SqrtDNode));
_nodeBytes->put(Op_SqrtF, sizeof(SqrtFNode));

_nodeBytes->put(Op_AryEq, sizeof(AryEqNode));
_nodeBytes->put(Op_CountPositives, sizeof(CountPositivesNode));
_nodeBytes->put(Op_StrComp, sizeof(StrCompNode));
_nodeBytes->put(Op_StrCompressedCopy, sizeof(StrCompressedCopyNode));
_nodeBytes->put(Op_StrEquals, sizeof(StrEqualsNode));
_nodeBytes->put(Op_StrIndexOfChar, sizeof(StrIndexOfCharNode));
_nodeBytes->put(Op_StrIndexOf, sizeof(StrIndexOfNode));
_nodeBytes->put(Op_StrInflatedCopy, sizeof(StrInflatedCopyNode));


_nodeBytes->put(Op_SubI, sizeof(SubINode));
_nodeBytes->put(Op_SubL, sizeof(SubLNode));
_nodeBytes->put(Op_ThreadLocal, sizeof(ThreadLocalNode));

_nodeBytes->put(Op_CompressBits, sizeof(CompressBitsNode));
_nodeBytes->put(Op_Con, sizeof(ConNode));
_nodeBytes->put(Op_ConstraintCast, sizeof(ConstraintCastNode));

_nodeBytes->put(Op_CreateEx, sizeof(CreateExNode));
_nodeBytes->put(Op_ExpandBits, sizeof(ExpandBitsNode));
_nodeBytes->put(Op_Phi, sizeof(PhiNode));
_nodeBytes->put(Op_RotateLeft, sizeof(RotateLeftNode));
_nodeBytes->put(Op_RotateRight, sizeof(RotateRightNode));
_nodeBytes->put(Op_SafePointScalarMerge, sizeof(SafePointScalarMergeNode));
_nodeBytes->put(Op_SafePointScalarObject, sizeof(SafePointScalarObjectNode));
_nodeBytes->put(Op_VectorCmpMasked, sizeof(VectorCmpMaskedNode));
_nodeBytes->put(Op_VectorMaskGen, sizeof(VectorMaskGenNode));
_nodeBytes->put(Op_VectorMaskOp, sizeof(VectorMaskOpNode));
_nodeBytes->put(Op_Vector, sizeof(VectorNode));
_nodeBytes->put(Op_UDivI, sizeof(UDivINode));
_nodeBytes->put(Op_UDivL, sizeof(UDivLNode));
_nodeBytes->put(Op_UModI, sizeof(UModINode));
_nodeBytes->put(Op_UModL, sizeof(UModLNode));
_nodeBytes->put(Op_UMulHiL, sizeof(UMulHiLNode));
_nodeBytes->put(Op_UpperCase, sizeof(UpperCaseNode));
_nodeBytes->put(Op_URShiftB, sizeof(URShiftBNode));
_nodeBytes->put(Op_URShiftI, sizeof(URShiftINode));
_nodeBytes->put(Op_URShiftL, sizeof(URShiftLNode));
_nodeBytes->put(Op_URShiftS, sizeof(URShiftSNode));
_nodeBytes->put(Op_VectorBox, sizeof(VectorBoxNode));
_nodeBytes->put(Op_VectorizedHashCode, sizeof(VectorizedHashCodeNode));
_nodeBytes->put(Op_VerifyVectorAlignment, sizeof(VerifyVectorAlignmentNode));
_nodeBytes->put(Op_Whitespace, sizeof(WhitespaceNode));

  _nodeBytes->put(Op_AbsVB, sizeof(AbsVBNode));
  _nodeBytes->put(Op_AbsVD, sizeof(AbsVDNode));
  _nodeBytes->put(Op_AbsVF, sizeof(AbsVFNode));
  _nodeBytes->put(Op_AbsVI, sizeof(AbsVINode));
  _nodeBytes->put(Op_AbsVL, sizeof(AbsVLNode));
  _nodeBytes->put(Op_AbsVS, sizeof(AbsVSNode));
  _nodeBytes->put(Op_AddVB, sizeof(AddVBNode));
  _nodeBytes->put(Op_AddVD, sizeof(AddVDNode));
  _nodeBytes->put(Op_AddVF, sizeof(AddVFNode));
  _nodeBytes->put(Op_AddVI, sizeof(AddVINode));
  _nodeBytes->put(Op_AddVL, sizeof(AddVLNode));
  _nodeBytes->put(Op_AddVS, sizeof(AddVSNode));
  _nodeBytes->put(Op_Allocate, sizeof(AllocateNode));
  _nodeBytes->put(Op_AllocateArray, sizeof(AllocateArrayNode));
  _nodeBytes->put(Op_AndI, sizeof(AndINode));
  _nodeBytes->put(Op_AndL, sizeof(AndLNode));

_nodeBytes->put(Op_AndV, sizeof(AndVNode));
_nodeBytes->put(Op_AndVMask, sizeof(AndVMaskNode));
_nodeBytes->put(Op_ArrayCopy, sizeof(ArrayCopyNode));
_nodeBytes->put(Op_CallDynamicJava, sizeof(CallDynamicJavaNode));
_nodeBytes->put(Op_CallJava, sizeof(CallJavaNode));
_nodeBytes->put(Op_CallLeaf, sizeof(CallLeafNode));
_nodeBytes->put(Op_CallLeafNoFP, sizeof(CallLeafNoFPNode));
_nodeBytes->put(Op_CallLeafVector, sizeof(CallLeafVectorNode));
_nodeBytes->put(Op_CallRuntime, sizeof(CallRuntimeNode));
_nodeBytes->put(Op_CallStaticJava, sizeof(CallStaticJavaNode));
_nodeBytes->put(Op_CastDD, sizeof(CastDDNode));
_nodeBytes->put(Op_CastFF, sizeof(CastFFNode));
_nodeBytes->put(Op_CastII, sizeof(CastIINode));
_nodeBytes->put(Op_CastLL, sizeof(CastLLNode));
_nodeBytes->put(Op_CastPP, sizeof(CastPPNode));
_nodeBytes->put(Op_CastVV, sizeof(CastVVNode));
_nodeBytes->put(Op_Catch, sizeof(CatchNode));
_nodeBytes->put(Op_CatchProj, sizeof(CatchProjNode));
_nodeBytes->put(Op_CheckCastPP, sizeof(CheckCastPPNode));
_nodeBytes->put(Op_ConD, sizeof(ConDNode));
_nodeBytes->put(Op_ConF, sizeof(ConFNode));
_nodeBytes->put(Op_ConL, sizeof(ConLNode));
_nodeBytes->put(Op_ConN, sizeof(ConNNode));
_nodeBytes->put(Op_ConNKlass, sizeof(ConNKlassNode));
_nodeBytes->put(Op_ConP, sizeof(ConPNode));
_nodeBytes->put(Op_ConvD2F, sizeof(ConvD2FNode));
_nodeBytes->put(Op_ConvD2I, sizeof(ConvD2INode));
_nodeBytes->put(Op_ConvD2L, sizeof(ConvD2LNode));
_nodeBytes->put(Op_ConvF2D, sizeof(ConvF2DNode));
_nodeBytes->put(Op_ConvF2I, sizeof(ConvF2INode));
_nodeBytes->put(Op_ConvF2L, sizeof(ConvF2LNode));
_nodeBytes->put(Op_ConvF2HF, sizeof(ConvF2HFNode));
_nodeBytes->put(Op_ConvHF2F, sizeof(ConvHF2FNode));
_nodeBytes->put(Op_ConvI2D, sizeof(ConvI2DNode));
_nodeBytes->put(Op_ConvI2F, sizeof(ConvI2FNode));
_nodeBytes->put(Op_ConvI2L, sizeof(ConvI2LNode));
_nodeBytes->put(Op_ConvL2D, sizeof(ConvL2DNode));
_nodeBytes->put(Op_ConvL2F, sizeof(ConvL2FNode));
_nodeBytes->put(Op_ConvL2I, sizeof(ConvL2INode));
_nodeBytes->put(Op_CountLeadingZerosV, sizeof(CountLeadingZerosVNode));
_nodeBytes->put(Op_CountTrailingZerosV, sizeof(CountTrailingZerosVNode));
_nodeBytes->put(Op_CountedLoop, sizeof(CountedLoopNode));
_nodeBytes->put(Op_CountedLoopEnd, sizeof(CountedLoopEndNode));
_nodeBytes->put(Op_DecodeN, sizeof(DecodeNNode));
_nodeBytes->put(Op_DecodeNKlass, sizeof(DecodeNKlassNode));
_nodeBytes->put(Op_DivModI, sizeof(DivModINode));
_nodeBytes->put(Op_DivModL, sizeof(DivModLNode));
_nodeBytes->put(Op_UDivModI, sizeof(UDivModINode));
_nodeBytes->put(Op_UDivModL, sizeof(UDivModLNode));
_nodeBytes->put(Op_EncodeP, sizeof(EncodePNode));
_nodeBytes->put(Op_EncodePKlass, sizeof(EncodePKlassNode));
_nodeBytes->put(Op_ExpandBitsV, sizeof(ExpandBitsVNode));
_nodeBytes->put(Op_CompressBitsV, sizeof(CompressBitsVNode));
_nodeBytes->put(Op_ExpandV, sizeof(ExpandVNode));
_nodeBytes->put(Op_CompressV, sizeof(CompressVNode));
_nodeBytes->put(Op_CompressM, sizeof(CompressMNode));
_nodeBytes->put(Op_FastLock, sizeof(FastLockNode));
_nodeBytes->put(Op_FastUnlock, sizeof(FastUnlockNode));
_nodeBytes->put(Op_CmpN, sizeof(CmpNNode));
_nodeBytes->put(Op_CmpD, sizeof(CmpDNode));
_nodeBytes->put(Op_CmpD3, sizeof(CmpD3Node));
_nodeBytes->put(Op_CmpF, sizeof(CmpFNode));
_nodeBytes->put(Op_CmpF3, sizeof(CmpF3Node));
_nodeBytes->put(Op_CmpI, sizeof(CmpINode));
_nodeBytes->put(Op_CmpL, sizeof(CmpLNode));
_nodeBytes->put(Op_CmpL3, sizeof(CmpL3Node));
_nodeBytes->put(Op_CmpP, sizeof(CmpPNode));
_nodeBytes->put(Op_CmpU, sizeof(CmpUNode));
_nodeBytes->put(Op_CmpU3, sizeof(CmpU3Node));
_nodeBytes->put(Op_CmpUL, sizeof(CmpULNode));
_nodeBytes->put(Op_CmpUL3, sizeof(CmpUL3Node));
_nodeBytes->put(Op_If, sizeof(IfNode));
_nodeBytes->put(Op_IfFalse, sizeof(IfFalseNode));
_nodeBytes->put(Op_IfTrue, sizeof(IfTrueNode));
  _nodeBytes->put(Op_Initialize, sizeof(InitializeNode));
_nodeBytes->put(Op_Jump, sizeof(JumpNode));
_nodeBytes->put(Op_JumpProj, sizeof(JumpProjNode));
_nodeBytes->put(Op_LoadB, sizeof(LoadBNode));
_nodeBytes->put(Op_LoadUB, sizeof(LoadUBNode));
_nodeBytes->put(Op_LoadUS, sizeof(LoadUSNode));
_nodeBytes->put(Op_LoadD, sizeof(LoadDNode));
_nodeBytes->put(Op_LoadD_unaligned, sizeof(LoadD_unalignedNode));
_nodeBytes->put(Op_LoadF, sizeof(LoadFNode));
_nodeBytes->put(Op_LoadI, sizeof(LoadINode));
_nodeBytes->put(Op_LoadKlass, sizeof(LoadKlassNode));
_nodeBytes->put(Op_LoadNKlass, sizeof(LoadNKlassNode));
_nodeBytes->put(Op_LoadL, sizeof(LoadLNode));
_nodeBytes->put(Op_LoadL_unaligned, sizeof(LoadL_unalignedNode));
_nodeBytes->put(Op_LoadP, sizeof(LoadPNode));
_nodeBytes->put(Op_LoadN, sizeof(LoadNNode));
_nodeBytes->put(Op_LoadRange, sizeof(LoadRangeNode));
_nodeBytes->put(Op_LoadS, sizeof(LoadSNode));
_nodeBytes->put(Op_Lock, sizeof(LockNode));
_nodeBytes->put(Op_LongCountedLoop, sizeof(LongCountedLoopNode));
_nodeBytes->put(Op_LongCountedLoopEnd, sizeof(LongCountedLoopEndNode));
_nodeBytes->put(Op_Mach, sizeof(MachNode));
_nodeBytes->put(Op_MachNullCheck, sizeof(MachNullCheckNode));
_nodeBytes->put(Op_MacroLogicV, sizeof(MacroLogicVNode));
_nodeBytes->put(Op_MaskAll, sizeof(MaskAllNode));
_nodeBytes->put(Op_MemBarAcquire, sizeof(MemBarAcquireNode));
_nodeBytes->put(Op_LoadFence, sizeof(LoadFenceNode));
_nodeBytes->put(Op_MemBarAcquireLock, sizeof(MemBarAcquireLockNode));
_nodeBytes->put(Op_MemBarCPUOrder, sizeof(MemBarCPUOrderNode));
_nodeBytes->put(Op_MemBarRelease, sizeof(MemBarReleaseNode));
_nodeBytes->put(Op_StoreFence, sizeof(StoreFenceNode));
_nodeBytes->put(Op_StoreStoreFence, sizeof(StoreStoreFenceNode));
_nodeBytes->put(Op_MemBarReleaseLock, sizeof(MemBarReleaseLockNode));
_nodeBytes->put(Op_MemBarVolatile, sizeof(MemBarVolatileNode));
_nodeBytes->put(Op_MemBarStoreStore, sizeof(MemBarStoreStoreNode));

  _nodeBytes->put(Op_MulVB, sizeof(MulVBNode));
_nodeBytes->put(Op_MulVS, sizeof(MulVSNode));
_nodeBytes->put(Op_MulVI, sizeof(MulVINode));
_nodeBytes->put(Op_MulVL, sizeof(MulVLNode));
_nodeBytes->put(Op_MulVF, sizeof(MulVFNode));
_nodeBytes->put(Op_MulVD, sizeof(MulVDNode));
_nodeBytes->put(Op_NegVI, sizeof(NegVINode));
_nodeBytes->put(Op_NegVL, sizeof(NegVLNode));
_nodeBytes->put(Op_NegVF, sizeof(NegVFNode));
_nodeBytes->put(Op_NegVD, sizeof(NegVDNode));
_nodeBytes->put(Op_SqrtVD, sizeof(SqrtVDNode));
_nodeBytes->put(Op_SqrtVF, sizeof(SqrtVFNode));
_nodeBytes->put(Op_LShiftCntV, sizeof(LShiftCntVNode));
_nodeBytes->put(Op_RShiftCntV, sizeof(RShiftCntVNode));
_nodeBytes->put(Op_LShiftVB, sizeof(LShiftVBNode));
_nodeBytes->put(Op_LShiftVS, sizeof(LShiftVSNode));
_nodeBytes->put(Op_LShiftVI, sizeof(LShiftVINode));
_nodeBytes->put(Op_LShiftVL, sizeof(LShiftVLNode));
_nodeBytes->put(Op_RShiftVB, sizeof(RShiftVBNode));
_nodeBytes->put(Op_RShiftVS, sizeof(RShiftVSNode));
_nodeBytes->put(Op_RShiftVI, sizeof(RShiftVINode));
_nodeBytes->put(Op_RShiftVL, sizeof(RShiftVLNode));
_nodeBytes->put(Op_URShiftVB, sizeof(URShiftVBNode));
_nodeBytes->put(Op_URShiftVS, sizeof(URShiftVSNode));
_nodeBytes->put(Op_URShiftVI, sizeof(URShiftVINode));
_nodeBytes->put(Op_URShiftVL, sizeof(URShiftVLNode));
_nodeBytes->put(Op_OrV, sizeof(OrVNode));
_nodeBytes->put(Op_XorV, sizeof(XorVNode));
_nodeBytes->put(Op_MinV, sizeof(MinVNode));
_nodeBytes->put(Op_MaxV, sizeof(MaxVNode));
_nodeBytes->put(Op_UMinV, sizeof(UMinVNode));
_nodeBytes->put(Op_UMaxV, sizeof(UMaxVNode));
_nodeBytes->put(Op_LoadVector, sizeof(LoadVectorNode));
_nodeBytes->put(Op_LoadVectorGather, sizeof(LoadVectorGatherNode));
_nodeBytes->put(Op_LoadVectorGatherMasked, sizeof(LoadVectorGatherMaskedNode));
_nodeBytes->put(Op_StoreVector, sizeof(StoreVectorNode));
_nodeBytes->put(Op_StoreVectorScatter, sizeof(StoreVectorScatterNode));
_nodeBytes->put(Op_StoreVectorScatterMasked, sizeof(StoreVectorScatterMaskedNode));
_nodeBytes->put(Op_LoadVectorMasked, sizeof(LoadVectorMaskedNode));
_nodeBytes->put(Op_StoreVectorMasked, sizeof(StoreVectorMaskedNode));
_nodeBytes->put(Op_VectorMaskTrueCount, sizeof(VectorMaskTrueCountNode));
_nodeBytes->put(Op_VectorMaskFirstTrue, sizeof(VectorMaskFirstTrueNode));
_nodeBytes->put(Op_VectorMaskLastTrue, sizeof(VectorMaskLastTrueNode));

_nodeBytes->put(Op_VectorMaskToLong, sizeof(VectorMaskToLongNode));
_nodeBytes->put(Op_VectorLongToMask, sizeof(VectorLongToMaskNode));
_nodeBytes->put(Op_Pack, sizeof(PackNode));
_nodeBytes->put(Op_PackB, sizeof(PackBNode));
_nodeBytes->put(Op_PackS, sizeof(PackSNode));
_nodeBytes->put(Op_PackI, sizeof(PackINode));
_nodeBytes->put(Op_PackL, sizeof(PackLNode));
_nodeBytes->put(Op_PackF, sizeof(PackFNode));
_nodeBytes->put(Op_PackD, sizeof(PackDNode));
_nodeBytes->put(Op_Pack2L, sizeof(Pack2LNode));
_nodeBytes->put(Op_Pack2D, sizeof(Pack2DNode));
_nodeBytes->put(Op_Replicate, sizeof(ReplicateNode));
_nodeBytes->put(Op_RoundVF, sizeof(RoundVFNode));
_nodeBytes->put(Op_RoundVD, sizeof(RoundVDNode));
_nodeBytes->put(Op_Extract, sizeof(ExtractNode));
_nodeBytes->put(Op_SelectFromTwoVector, sizeof(SelectFromTwoVectorNode));
_nodeBytes->put(Op_VectorBoxAllocate, sizeof(VectorBoxAllocateNode));
_nodeBytes->put(Op_VectorUnbox, sizeof(VectorUnboxNode));
_nodeBytes->put(Op_VectorMaskWrapper, sizeof(VectorMaskWrapperNode));
_nodeBytes->put(Op_VectorMaskCmp, sizeof(VectorMaskCmpNode));
_nodeBytes->put(Op_VectorMaskCast, sizeof(VectorMaskCastNode));
_nodeBytes->put(Op_VectorTest, sizeof(VectorTestNode));
_nodeBytes->put(Op_VectorBlend, sizeof(VectorBlendNode));
_nodeBytes->put(Op_VectorRearrange, sizeof(VectorRearrangeNode));
_nodeBytes->put(Op_VectorLoadMask, sizeof(VectorLoadMaskNode));
_nodeBytes->put(Op_VectorLoadShuffle, sizeof(VectorLoadShuffleNode));
_nodeBytes->put(Op_VectorLoadConst, sizeof(VectorLoadConstNode));
_nodeBytes->put(Op_VectorStoreMask, sizeof(VectorStoreMaskNode));
_nodeBytes->put(Op_VectorReinterpret, sizeof(VectorReinterpretNode));
_nodeBytes->put(Op_VectorCast, sizeof(VectorCastNode));
_nodeBytes->put(Op_VectorCastB2X, sizeof(VectorCastB2XNode));
_nodeBytes->put(Op_VectorCastS2X, sizeof(VectorCastS2XNode));
_nodeBytes->put(Op_VectorCastI2X, sizeof(VectorCastI2XNode));
_nodeBytes->put(Op_VectorCastL2X, sizeof(VectorCastL2XNode));
_nodeBytes->put(Op_VectorCastF2X, sizeof(VectorCastF2XNode));
_nodeBytes->put(Op_VectorCastD2X, sizeof(VectorCastD2XNode));
_nodeBytes->put(Op_VectorCastF2HF, sizeof(VectorCastF2HFNode));
_nodeBytes->put(Op_VectorCastHF2F, sizeof(VectorCastHF2FNode));
_nodeBytes->put(Op_VectorUCastB2X, sizeof(VectorUCastB2XNode));
_nodeBytes->put(Op_VectorUCastS2X, sizeof(VectorUCastS2XNode));
_nodeBytes->put(Op_VectorUCastI2X, sizeof(VectorUCastI2XNode));
_nodeBytes->put(Op_VectorInsert, sizeof(VectorInsertNode));
_nodeBytes->put(Op_OrVMask, sizeof(OrVMaskNode));
_nodeBytes->put(Op_XorVMask, sizeof(XorVMaskNode));


}


//______________________________________________Auxiliary Class_________________________________________________




