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

void SonSerializer:: dump(){

	return;
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
    /*bool  _traverse_outs=true;
    if (_traverse_outs) {//default true
      for (DUIterator i = n->outs(); n->has_out(i); i++) {
        nodeStack.push(n->out(i));
      }
    }*/
    for (uint i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        ++_edgeNum;
      }
  }
}


void SonSerializer::visit_node(Node* n, bool edges) {

  Node *node = n;
  _output->print_cr("Index:%d",node->_idx);
  _output->print("Input:");
  for (uint i=0;i<node->len();++i) {
    if (node->in(i)!=nullptr)
        _output->print("%d,",node->in(i)->_idx);
  }
  _output->print_cr("");
}

void SonSerializer::set_csr() {
  _isCSR=true;
  _graph=new CSRGraph(_root,_nodeNum,_edgeNum,_maxNodeIdx,C);
}


//______________________________________________Graph Storage_________________________________________________
CSRGraph::CSRGraph(Node* nd,int nodeNumber,int edgeNumber,int maxNodeIdx,Compile* C)
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

    for (uint i = 0; i < n->len(); i++)
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
        _edge[pend++]=n->in(i)->_idx;
      }

    _oriOffset[n->_idx]=pstart;
    pstart=pend;
  }
  reassign_idx();
  recover_idx();
}


//return i, _oriOffset[i] is the lowest upper bound of num.
//_oriOffset[i] cannot be equal to num.
int CSRGraph::find_lowest_upper_bound(int num,bool equal) {
  int low=INT_MAX,idx=-1;
  for (uint i=0;i<=_maxNodeIdx;++i)
    if (((equal&&_oriOffset[i]>=num)||(!equal&&_oriOffset[i]>num))&&_oriOffset[i]<low) {
      low=_oriOffset[i];
      idx=i;
    }
  return idx;
}

int CSRGraph::lookup_idx_hash(int old) {
  for (int i=0;i<_nodeNumber;++i)
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

  for (int p=1;p<_nodeNumber;++p) {
    _idxHash[p]=find_lowest_upper_bound(_oriOffset[_idxHash[p-1]],0);
    _newOffset[p]=_oriOffset[_idxHash[p]];
  }

  //modify the edge to replace the old indices with the newly-assigned indices, O(n^2)
  //the newEdge can be deleted after varification.

  _newEdge=(int*)C->comp_arena()->Amalloc(sizeof(int)*_edgeNumber);
  for (int i=0;i<_edgeNumber;++i)
    _newEdge[i]=lookup_idx_hash(_edge[i]);

}
//for now, just to be used to validate the correctness, compared with _oriOffset and _edge.
void CSRGraph::recover_idx() {
  bool good=true;
  //validate edge
  for (int i=0;i<_edgeNumber;++i)
    if (_edge[i]!=_idxHash[_newEdge[i]]) {
      good=false;
      break;
    }
  //validate the idx
  //how to get _oriOffset with _newOffset and _idxHash?
  for (int i=0;i<=_nodeNumber;++i)
    if (_oriOffset[_idxHash[i]]!=_newEdge[i]) {
      good =false;
      break;
    }


  if (!good) {

    outputStream* _output = new (mtCompiler) fileStream("validate.txt","w");
    _output->print_cr("incorrect!!!!");
  }
}





