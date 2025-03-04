//
// Created by wlr on 2/19/25.
//
#ifndef SONSERIALIZER_H
#define SONSERIALIZER_H
#include "utilities/ostream.hpp"
#include "runtime/threadCritical.hpp"
#include "runtime/threadSMR.hpp"
#include "opto/node.hpp"
#include "opto/compile.hpp"
#include "libadt/dict.hpp"
#include "libadt/vectset.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"
#include "utilities/xmlstream.hpp"
#include "memory/resourceArea.hpp"
#include "opto/chaitin.hpp"
#include "opto/idealGraphPrinter.hpp"
#include "opto/machnode.hpp"
#include "opto/parse.hpp"
#include "runtime/threadCritical.hpp"
#include "runtime/threadSMR.hpp"
#include "utilities/stringUtils.hpp"
class Graph;
class CSRGraph;

//__________________________________________________Control_____________________________________________________
class SonSerializer:public ResourceObj{
private:
	Node* _root=nullptr;//coding style! add underscores
	outputStream*_output;
	char _buffer[512];
	int _nodeNum=0;
	uint _maxNodeIdx=0;
	int _edgeNum=0;
	bool _isCSR=false;
	Compile* C;
	Graph * _graph=nullptr;

public:

	SonSerializer(Compile* compile, const char* file_name=nullptr);
	~SonSerializer();
	void walk_nodes(Node* root);
	void visit_node(Node* n,bool edges);
	void set_csr();
	void set_compile(Compile* compile) {C = compile; }
	void dump();

};
//______________________________________________Graph Storage_________________________________________________
class Graph:public ResourceObj {
private:
public:
//	virtual void setCompressionStrategy()=0;
//	virtual void compress()=0;
	virtual void reassign_idx ()=0;
	virtual void recover_idx()=0;
};
//CSR format
class CSRGraph:public Graph {
private:
	int *_oriOffset=nullptr;//_oriOffset[idx] points to the start of the outgoing edges. Original.
	int *_edge=nullptr;//_egde[_oriOffset[idx]]~_edge[_oriOffset[lowest upper bound of idx]] is the outgoing edges of idx.

	int *_newOffset=nullptr;//after index reassign.
	int *_newEdge=nullptr;//for current phase, just for validation.
	int *_idxHash=nullptr;//size=_nodeNumber, _idxHash[newIdx]=oldIdx.
	int _nodeNumber;
	int _edgeNumber;
	uint _maxNodeIdx;//idx starts from 0. And _oriOffset[_maxNodeIdx] should be valid.
	Compile* C;

public:
	CSRGraph(Node* nd,int nodeNumber,int edgeNumber,int maxNodeIdx,Compile* C);
	void reassign_idx ();
	void recover_idx ();


private:
	int find_lowest_upper_bound(int num,bool equal);
	int lookup_idx_hash(int old);

};










#endif //SONSERIALIZER_H
