//
// Created by wlr on 2/19/25.
//
#include "opto/sonserializer.hpp"

#include <algorithm>
#include <bits/ctype_base.h>
//if you want to sort the edge, then U have to store all the index. No exception, no priminary knowledge
#define SORTTHEEDGE true
// combine sorting and preliminary knowledge
#define COMBINATION false
#define PRELIMINARY false

#include "runtime/globals_extension.hpp"

SonSerializer::SonSerializer(Compile* compile,const char* file_name){
	_root = (Node*)compile->root();
  this->set_compile((compile));
  walk_nodes(_root);
}

SonSerializer:: ~SonSerializer()
{

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
  _f=new (mtCompiler) fileStream("edge.txt","w");
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
#if !SORTTHEEDGE
    if (preliminary_known_node(n)==false) {
      uint empty=0;
      uint filled=0;
      for (uint i = 0; i < n->len(); i++)
        if (n->in(i) != nullptr) ++filled;
        else ++empty;
      if (empty>filled)
        _edgeIdxMask->set(offsetP);
      else _edgeIdxMask->clear(offsetP);
    }//to be recovered
#endif

    //traverse the output edges make sure no nodes are omitted
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));

    for (uint i = 0; i < n->len(); i++) {
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        _oriEdge[pend++]=n->in(i)->_idx;
#if SORTTHEEDGE
        _edgeIdx[maskp++]=i;
      }
    }
#else
        if (_edgeIdxMask->get(offsetP))//to be recovered
        //if (!preliminary_known_node(n))//to be deleted
          _edgeIdx[maskp++]=i;//_edgeIdx and the _oriEdge and _edge actually preserve the same order, dont modify them for now
      }
      else if (!preliminary_known_node(n)&&!_edgeIdxMask->get(offsetP)) {//set the empty slot as -1
        _oriEdge[pend++]=-1;
      }//to be recovered
    }
#endif
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
  store_offset();
#if SORTTHEEDGE
  store_sorted_edge();
#endif

#if COMBINATION
  store_partly_sorted_edge();
#endif

#if PRELIMINARY
  store_edgeIdx();
  store_edge();
#endif


  _f->close();
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
    *reinterpret_cast<int*>((char*)node+32)=0;//set _outcnt 0
    *reinterpret_cast<int*>((char*)node+36)=0;//set _outmax
    *reinterpret_cast<int*>((char*)node+40)=_idHash[i];
  }
  f->close();
  uint pIdx=0;//index the edgeIdx
  for (uint i=0;i<_nodeNum;++i) {
    //first get the sizeof the input(maybe contain -1
   // if (nodeSet->at(i)->Opcode()==Op_Start)
    //  tty->print_cr("hello, we are now at this point");
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
#if !SORTTHEEDGE
    if(_edgeIdxMask->get(i)) {//the idx is stored, case 1 not preliminary but filled<empty// to be recovered
#endif
    //if (!preliminary_known_node(nodeSet->at(i))){//to be deleted
      for (uint j=pIdx;j<pIdx+sz;++j) {
        if (_edgeIdx[j]<static_cast<int>(nodeSet->at(i)->req()))
            nodeSet->at(i)->init_req(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
        else
          nodeSet->at(i)->set_prec(_edgeIdx[j],nodeSet->at(_edge[pEdge++]));
      }
      pIdx+=sz;
#if !SORTTHEEDGE
    }
#endif
    //nooooooo, the idx is not stored, so you have to analysis.
    //two cases:
    //case 0:preliminary,add,sub...
    //case 2:-1.
#if !SORTTHEEDGE
    else if (preliminary_known_node(nodeSet->at(i))){
      //add sub...*///to be recovered
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
    }//to be recovered
#endif
  }

  C->set_root(static_cast<RootNode*>(nodeSet->at(0)));
  return 0;

}



void CSRGraph::store_node(Node* n,fileStream* f){
  ushort opcode=n->Opcode();
  f->write((char*)&opcode,2);
  int sz=*(_nodeBytes->get(opcode));

  f->write((char*)n+16,16);//in release mode if we omit _out, bug will appear


  f->write((char*)n+44,4);
  f->write((char*)n+52,sz-52);
//vt pointer, offset 0
 /* f->write((char*)n+24,8);
  f->write((char*)n+40,sz-40);*/
  //f->write((char*)n+48,12);//shoud be 12
  //f->write((char*)n+64,sz-64);
}

void CSRGraph::load_node(Node*n, fileStream*f,int sz,ushort op) {
  *(void**)n=_vptrTable[op];

  //f->read((char*)n,sizeof(char),sz);
  f->read((char*)n+16,sizeof(char),16);


  f->read((char*)n+44,sizeof(char),4);
  f->read((char*)n+52,sizeof(char),sz-52);
  *(Node***)((char*)n+8)=*(Node***)((char*)_root+8);//to satisfy a requirement when setting top node

  /*f->read((char*)n+24,sizeof(char),8);


  f->read((char*)n+40,sizeof(char),sz-40) ;*/

  // *(juint*)((char*)n+44)=*(juint*)((char*)n+40);

  //f->read((char*)n+48,sizeof(char),12);
  //*(uint*)((char*)n+60)=0xf1f1f1f1;
  //f->read((char*)n+64,sizeof(char),sz-64);
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



//______________________________________________Auxiliary Member Function________________________________________________
//give it an array and the size, it will do inplace kbit-encoding and return the kbit length
int CSRGraph::kbit_encoding(int* a,int sz) {
  int pBytes=0;
  for (int i=0;i<sz;++i) {
    int obj=a[i];
    bool neg=obj<0;
    if (neg) {
      obj=~obj+1;//complement->source
    }
    int shift=0;
    for (int b=0;b<4;++b,++pBytes){
      u_int8_t* cur=(u_int8_t*)a+pBytes;
      if (b==0) {
        *cur=obj&0x3f;
        if (neg) set_bit(cur,6);
        shift+=6;
      }
      else {
        if ((obj&(0x7f<<shift))!=0) {
          set_bit(cur-1,7);
          *cur=(obj&(0x7f<<shift))>>shift;
          shift+=7;
        }
        else break;
      }
    }
  }
  return pBytes;

}
void CSRGraph::kbit_decoding(int* a,int sz) {
  int *tmpEdge = (int*)C->comp_arena()->Amalloc(sizeof(int)*sz);
  uint pInt=0;
  uint pByte=0;
  for (int i=0;i< sz;++i) {//for each node
      int obj=0;
      int shift=0;
      bool neg=false;
      bool isFirstByte=true;
      while(1){//decoding an integer idx.for each byte.
        u_int8_t * cur=(u_int8_t*)a+pByte;
        if(isFirstByte){
          neg=(*cur&0x40)!=0;
          obj+=*cur&0x3f;
        }
        else obj+=(*cur&0x7f)<<shift;
        ++pByte;
        if((*cur&0x80)==0) break;
        shift+=isFirstByte?6:7;
        isFirstByte=false;
      }
      tmpEdge[pInt++]=neg?-obj:obj;//recover obj(complement version) by i-obj
  }
  memcpy(a,tmpEdge,sizeof(int)*sz);
  C->comp_arena()->Afree(tmpEdge,sizeof(int)*sz);

}


int CSRGraph::kbit_encoding_pos(int* a,int sz) {
  int pBytes=0;
  for (int i=0;i<sz;++i) {
    int obj=a[i];
    int shift=0;
    for (int b=0;b<4;++b,++pBytes){
      u_int8_t* cur=(u_int8_t*)a+pBytes;
      if (b==0) {
        *cur=obj&0x7f;
        shift+=7;
        continue;
      }
      if ((obj&(0x7f<<shift))!=0) {
        set_bit(cur-1,7);
        *cur=(obj&(0x7f<<shift))>>shift;
        shift+=7;
      }
      else break;

    }
  }
  return pBytes;
}
void CSRGraph::kbit_decoding_pos(int* a,int sz) {
  int *tmpEdge = (int*)C->comp_arena()->Amalloc(sizeof(int)*sz);
  uint pInt=0;
  uint pByte=0;
  for (int i=0;i< sz;++i) {//for each node
    int obj=0;
    int shift=0;

    while(1){//decoding an integer idx.for each byte.
      u_int8_t * cur=(u_int8_t*)a+pByte;
      obj+=(*cur&0x7f)<<shift;
      ++pByte;
      if((*cur&0x80)==0) break;
      shift+=7;
    }
    tmpEdge[pInt++]=obj;
  }
  memcpy(a,tmpEdge,sizeof(int)*sz);
  C->comp_arena()->Afree(tmpEdge,sizeof(int)*sz);

}

//4bits-version kbit-encoding, return the 4bits blocks' num
int CSRGraph::bit4_encoding(int* a, int sz) {
  int out_idx = 0;      // index into int[]
  int bit_pos = 0;      // bit position within current int
  int buffer = 0;       // current 32-bit output buffer
  for (int i = 0; i < sz; ++i) {
    int val = a[i];
    do {
      int chunk = val & 0x7; // lower 3 bits
      val >>= 3;
      int nibble = (val != 0) ? (0x8 | chunk) : chunk;
      buffer |= (nibble << bit_pos);
      bit_pos += 4;

      if (bit_pos == 32) {
        a[out_idx++] = buffer;
        buffer = 0;
        bit_pos = 0;
      }
    } while (val != 0);
  }

  if (bit_pos > 0) {
    a[out_idx++] = buffer;
  }

  return out_idx;
}



void CSRGraph::bit4_decoding(int* a, int sz) {
  int read_idx = 0;
  int bit_pos = 0;
  int buffer = a[0];

  int *temp = (int*)C->comp_arena()->Amalloc(sizeof(int)*sz);
  int write_idx = 0;
  int acc = 0;
  int shift = 0;

  while (write_idx < sz) {
    if (bit_pos >= 32) {
      buffer = a[++read_idx];
      bit_pos = 0;
    }

    int nibble = (buffer >> bit_pos) & 0xF;
    bit_pos += 4;

    acc |= (nibble & 0x7) << shift;
    shift += 3;

    if ((nibble & 0x8) == 0) {
      temp[write_idx++] = acc;
      acc = 0;
      shift = 0;
    }

  }

  // Copy back to a[]
  for (int i = 0; i < sz; ++i) {
    a[i] = temp[i];
  }
  C->comp_arena()->Afree(temp,sizeof(int)*sz);
}

void CSRGraph::store_offset() {
  for (int i=_nodeNum-1;i>0;--i)
    _offset[i]-=_offset[i-1];//convert absolute value to relative delta
  //int sz=kbit_encoding(_offset,_nodeNum);
  int sz=bit4_encoding(_offset,_nodeNum)<<2;
  _f->write((char*)_offset, sz);
  bit4_decoding(_offset,_nodeNum);
  for (uint i=1;i<_nodeNum;++i)
    _offset[i]+=_offset[i-1];

}

void CSRGraph::store_edgeIdx() {
 // int sz=kbit_encoding(_edgeIdx,_edgeIdxSize);
 int sz=bit4_encoding(_edgeIdx,_edgeIdxSize)<<2;
  _f->write((char*)_edgeIdx, sz);
  bit4_decoding(_edgeIdx,_edgeIdxSize);
}

void CSRGraph::store_edge() {//use this version, there should no -1
 /* for (uint i=0;i<_nodeNum;++i) {
    int start=_offset[i];
    int end=((i==_nodeNum-1)?_edgeNum:_offset[i+1])-1;//[start,end]
    int pre=_edge[end];
    int p=end;
    while ((--p)>=start) {
      //if (_edge[p]==-1) continue;
      int temp=pre;
      pre=_edge[p];
      //uint flag=_edge[p]>temp?1:0;//1:positive value, 0: negtive value
      //_edge[p]=((flag?(_edge[p]-temp):(temp-_edge[p]))<<1)|flag;
      _edge[p]-=temp;

    }
  }*/
  int sz=kbit_encoding(_edge,_edgeNum);
  _f->write((char*)_edge, sz);
  kbit_decoding(_edge,_edgeNum);

 /* for (uint i=0;i<_nodeNum;++i) {
    int start=_offset[i];
    int end=((i==_nodeNum-1)?_edgeNum:_offset[i+1])-1;//[start,end]
    int pre=_edge[end];
    int p=end;
    while ((--p)>=start) {
      //if (_edge[p]==-1) continue;
      //uint flag=_edge[p]&0x1;
      //if (flag)
        //_edge[p]=(_edge[p]>>1)+pre;
      //else _edge[p]=pre-(_edge[p]>>1);
      _edge[p]+=pre;
      pre=_edge[p];
    }
  }
//may not strictly decreasing*/
}

void sort(int* main,int *sub,int sz) {//ascending order
  for (int i = 0; i < sz - 1; ++i) {
    for (int j = 0; j < sz - i - 1; ++j) {
      if (main[j] > main[j + 1]) {
        // Swap main elements
        int tmp = main[j];
        main[j] = main[j + 1];
        main[j + 1] = tmp;

        // Swap corresponding sub elements
        tmp = sub[j];
        sub[j] = sub[j + 1];
        sub[j + 1] = tmp;
      }
    }
  }

}



void CSRGraph::store_sorted_edge() {//if you take this way the kbit encoding should be all positive( and it will reduce bytes
//pre-processing
  int pIdx=0;//index the _edgeIdx
  for (uint i=0;i<_nodeNum;++i) {
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    //the idx is stored, case 1 not preliminary but filled<empty
    sort(_edge+pEdge,_edgeIdx+pIdx,sz);
    pIdx+=sz;
    for (uint j=pEdge+sz-1;j>pEdge;--j)
      _edge[j]-=_edge[j-1];
  }
  store_edgeIdx() ;
  int sz=kbit_encoding_pos(_edge,_edgeNum);
  _f->write((char*)_edge, sz);
  kbit_decoding_pos(_edge,_edgeNum);

  //post-processing
  pIdx=0;
  for (uint i=0;i<_nodeNum;++i) {
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    for (uint j=pEdge+1;j<pEdge+sz;++j)
      _edge[j]+=_edge[j-1];
    sort(_edgeIdx+pIdx,_edge+pEdge,sz);
    pIdx+=sz;
  }

}


void CSRGraph::store_partly_sorted_edge() {
  int pIdx=0;//index the _edgeIdx
  for (uint i=0;i<_nodeNum;++i) {
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    if(_edgeIdxMask->get(i)) {//the idx is stored, case 1 not preliminary but filled<empty// to be recovered
      sort(_edge+pEdge,_edgeIdx+pIdx,sz);
      pIdx+=sz;
      for (uint j=pEdge+sz-1;j>pEdge;--j)
        _edge[j]-=_edge[j-1];
    }
  }

  store_edgeIdx() ;
  int sz=kbit_encoding(_edge,_edgeNum);
  _f->write((char*)_edge, sz);
  kbit_decoding(_edge,_edgeNum);

  pIdx=0;
  for (uint i=0;i<_nodeNum;++i) {
    uint sz= (i==_nodeNum-1?_edgeNum:_offset[i+1]) -_offset[i];
    uint pEdge=_offset[i];
    if (_edgeIdxMask->get(i)) {
      for (uint j=pEdge+1;j<pEdge+sz;++j)
        _edge[j]+=_edge[j-1];
      sort(_edgeIdx+pIdx,_edge+pEdge,sz);
      pIdx+=sz;
    }
  }


}