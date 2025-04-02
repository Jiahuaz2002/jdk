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
  :_root(nd),_nodeNum(nodeNumber),_edgeNum(edgeNumber),_maxNodeIdx(maxNodeIdx),C(C)
{
  Node* start=nd;
  _oriOffset=(int*)C->comp_arena()->Amalloc(sizeof(int)*(1+_maxNodeIdx));
  memset(_oriOffset,-1,sizeof(int)*(1+_maxNodeIdx));
  _oriEdge=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNum);
  _edgeIdx=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNum);
  _edgeIdxMask=new Bitmask(C,_maxNodeIdx);

  uint pstart=0;//for edge and offset
  uint pend=0;

  uint maskp=0;

//create the csr format.
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(start);

  while (nodeStack.length() > 0) {//start traversing the graph
    Node* n = nodeStack.pop();

    if (visited.test_set(n->_idx))//test and set
      continue;

    if (need_input_index(n))
      //then u can directly lookup the bitmask to know if this node(with _idx) has stored its index
      _edgeIdxMask->set(n->_idx);
    //if we need to store the indices, there are two cases:
    //1.filled slots > empty slots, just set the empty slots as -1(like pointing to a null Node
    //2.empty slots > filled slots, just to store the filled slots indices
    bool nullflag=0;
    uint empty=0;
    uint filled=0;
    for (uint i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) ++filled;
      else ++empty;
    if (empty<filled) nullflag=true;



    //traverse the output edges make sure no nodes are omitted
    for (uint i=0;i<n->outcnt();++i)
      nodeStack.push(n->raw_out(i));

    for (uint i = 0; i < n->len(); i++) {//how to store the mask depends on how to decoding....
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        _oriEdge[pend++]=n->in(i)->_idx;
        if (_edgeIdxMask->get(n->_idx))
          _edgeIdx[maskp++]=i;
      }
      else if (nullflag) {//set the empty slot as -1
        _oriEdge[pend++]=-1;
      }

    }

    _oriOffset[n->_idx]=pstart;
    pstart=pend;
  }
  _edgeIdxSize=maskp;//totally how many indices are stored

}
CSRGraph::~CSRGraph() {
 // if (_edgeIdxMask) delete _edgeIdxMask;
}


void CSRGraph::compress_and_dump() {
  reassign_idx();
 // kbit_encoding();
  //kbit_decoding();
  Huffman tmp(_edgeIdx,_edgeIdxSize,C);//use huffman to encode the index
  tmp.encode();
  recover_idx();
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
  for (uint i=0;i<_nodeNum;++i)
    if (old==_idxHash[i])
      return i;
  return -1;
}


//The first step, only ensure the _oriOffset[i+1]-_oriOffset[i] is the outEdgeNum of node i.
//To look up the very rudimentary hash table, new->old O(1), old->new O(n)
//Restore:to be written...
//The hash table Can be further optimize by compressing bit.(after ask how many num son can reach...

void CSRGraph::reassign_idx(){
  //construct _offset and _idxHash, O(n^2)
  _idxHash=(int*)C->comp_arena()->Amalloc(sizeof(int)*_nodeNum);
  _offset=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNum);
  _idxHash[0]=find_lowest_upper_bound(0,1);
  _offset[0]=0;

  for (uint p=1;p<_nodeNum;++p) {
    _idxHash[p]=find_lowest_upper_bound(_oriOffset[_idxHash[p-1]],0);
    _offset[p]=_oriOffset[_idxHash[p]];
  }

  //modify the edge to replace the old indices with the newly-assigned indices, O(n^2)
  //the _edge can be deleted after verification.

  _edge=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNum);
  for (uint i=0;i<_edgeNum;++i)
    _edge[i]=lookup_idx_hash(_oriEdge[i]);

}
//for now, just to be used to validate the correctness, compared with _oriOffset and _edge.
void CSRGraph::recover_idx() {
  bool good=true;
  //validate edge
  for (uint i=0;i<_edgeNum;++i)
    if (_oriEdge[i]!=_idxHash[_edge[i]]) {
      good=false;
      break;
    }
  //validate the idx
  //how to get _oriOffset with _offset and _idxHash?
  for (uint i=0;i<_nodeNum;++i)
    if (_oriOffset[_idxHash[i]]!=_offset[i]) {
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



//______________________________________________Auxiliary Class_________________________________________________
MinHeap::MinHeap(uint *data, uint *freqs, uint n,Compile* C):_size(n),C(C) {
  _heap= (TreeNode**)C->comp_arena()->Amalloc(sizeof(TreeNode*)*n);
  for (uint i = 0; i < n; i++)
    _heap[i] = new TreeNode(data[i], freqs[i]);

  for (int i = _size /2 - 1; i >= 0; i--)
    heapify(i);

}

void MinHeap::heapify(uint i) {
  uint smallest = i, left = (i<<1) + 1, right = (i<<1) + 2;
  if (left < _size && _heap[left]->freq < _heap[smallest]->freq)
    smallest = left;
  if (right < _size && _heap[right]->freq < _heap[smallest]->freq)
    smallest = right;
  if (smallest != i) {
    TreeNode* temp = _heap[i];
    _heap[i] = _heap[smallest];
    _heap[smallest] = temp;
    heapify(smallest);
  }

}


TreeNode* MinHeap::get_top() {
  TreeNode* min = _heap[0];
  _heap[0] = _heap[_size--];
  heapify(0);
  return min;
}

void MinHeap::insert(TreeNode* node) {
  uint i = ++_size;
  while (i && node->freq < _heap[(i - 1) / 2]->freq) {
    _heap[i] = _heap[(i - 1) / 2];
    i = (i - 1) / 2;
  }
  _heap[i] = node;
}


TreeNode *MinHeap::build_huffman_tree() {
  while (_size>1) {
    TreeNode* left = get_top();
    TreeNode* right = get_top();
    TreeNode* newNode = new TreeNode(-1, left->freq + right->freq);
    newNode->left = left;
    newNode->right = right;
    insert(newNode);
  }
  return get_top();
}


Huffman::Huffman(int* data, uint size,Compile* C) :C(C){
  int max=INT_MIN;
  for (uint i=0;i<size;++i)
    max=data[i]>max?data[i]:max;
  int *hash=(int*)C->comp_arena()->Amalloc(sizeof(int)*max);
  memset(hash,-1,sizeof(uint)*max);

  for (uint i=0;i<size;++i)
    ++hash[data[i]];

  uint num=0;
  for (uint i=0;i<size;++i)
    if (hash[i]!=-1) ++num;

  uint* value=(uint*)C->comp_arena()->Amalloc(sizeof(uint)*num);
  uint* freq=(uint*)C->comp_arena()->Amalloc(sizeof(uint)*num);

  for (uint i=0;i<size;++i)
    if (hash[i]!=-1) {
      value[i]=i;
      freq[i]=hash[i];
    }

  _heap=new MinHeap(value,freq,num,C);
  _root=_heap->build_huffman_tree();
  C->comp_arena()->Afree(hash,sizeof(uint)*max);
  C->comp_arena()->Afree(value,sizeof(uint)*num);
  C->comp_arena()->Afree(freq,sizeof(uint)*num);

}

void Huffman::code_gen(TreeNode *node, uint len, ushort data) {
  if (node==nullptr) return;//left edge 1, right edge 0
  if (node->left) code_gen(node->left, len+1,(data<<1)|0x1);
  if (node->right) code_gen(node->right,len+1,data<<1);
  if (!node->left&&!node->right) {
    codes[node->value].data=data;
    codes[node->value].len=len;
    _huffmanCodeLen+=len;
  }
  return;
}

uint Huffman::encode() {//now, only return the bytes number
  _huffmanCodeLen=0;
  code_gen(_root,0,0);
  return _huffmanCodeLen;
}

uint Huffman::decode() {

//now it is unnecessary as if I just want to know how much the graph can be compressed.
  //you can get the huffmancode and traverse the tree to decoding
return 0;
}


void HeuristicCSRGraph::optimizeIndices(int *Offset, int *Edge) {


//bfs? arrange interconnected nodes in adjacent positions to reduce differences.
//sorting? the node has most output assign the smallest values?
  //
  //Simulated Annealing? too complex/..


}






