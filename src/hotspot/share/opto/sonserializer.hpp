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
#include <bits/stdint-intn.h>
class Graph;
class CSRGraph;
class Bitmask;
class Huffman;
//__________________________________________________Control_____________________________________________________
class SonSerializer:public ResourceObj{
private:
	Node* _root=nullptr;//coding style! add underscores
	outputStream*_output;
	char _buffer[512];
	uint _nodeNum=0;
	uint _maxNodeIdx=0;
	uint _edgeNum=0;
	bool _isCSR=false;
	Compile* C;
	Graph * _graph=nullptr;

public:
	void set_csr();

	SonSerializer(Compile* compile, const char* file_name=nullptr);
	~SonSerializer();
	void walk_nodes(Node* root);
	void set_compile(Compile* compile) {C = compile; }
	void compress_and_dump();


};
//______________________________________________Graph Storage_________________________________________________
class Graph:public ResourceObj {
private:
public:
//	virtual void setCompressionStrategy()=0;
	virtual void compress_and_dump()=0;

};
//CSR format
class CSRGraph:public Graph {
private:
	Node* _root=nullptr;
	int *_oriOffset=nullptr;//_oriOffset[idx] points to the start of the outgoing edges. Original.
	int *_oriEdge=nullptr;//_oriEgde[_oriOffset[idx]]~_oriEdge[_oriOffset[lowest upper bound of idx]] is the outgoing edges of idx.


	int *_offset=nullptr;//after index reassign.
	int *_edge=nullptr;//for current phase, just for validation.

	int *_edgeIdx=nullptr;//slot index
	Bitmask* _edgeIdxMask;
	uint _edgeIdxSize;//actual num of the stored idx


	int *_idxHash=nullptr;//size=_nodeNum, _idxHash[newIdx]=oldIdx.
	uint _nodeNum;
	uint _edgeNum;
	uint _maxNodeIdx;//idx starts from 0. And _oriOffset[_maxNodeIdx] should be valid.
	Compile* C;

	uint _kbitBytesLen;//_after kbit-encoding the Bytes length of the _offset. _kbitBytesLen-1 is the final index.

public:
	CSRGraph(Node* nd,uint nodeNumber,uint edgeNumber,uint maxNodeIdx,Compile* C);
	void compress_and_dump()override;
	~CSRGraph();


private:
	//helping functions
	int find_lowest_upper_bound(  int num,  bool equal)const ;
	int lookup_idx_hash( int old)const ;
	void set_bit(u_int8_t* obj, int bit);//bit from 0->7, set bit from 0->1

	bool need_input_index(Node* node);

	//uint dumpNodeAttributes();//input is the root node
	void reassign_idx ();
	void recover_idx ();
	void kbit_encoding();
	void kbit_decoding();


};
//______________________________________________Auxiliary Class_________________________________________________
class Bitmask: public ResourceObj {//Bitmap indicating if the node's input index should be stored.

private:
	Compile* C;
	char *_mask=nullptr;
	uint _size;
public:
	Bitmask(Compile* C, uint SIZE):C(C),_size(SIZE){
		_mask = (char*)C->comp_arena()->Amalloc(sizeof(char)*((_size+7)>>3));//(Node+7)/8, nodeNum bits?
	}
	~Bitmask() {
		C->comp_arena()->Afree(_mask,sizeof(char)*((_size+7)>>3));
	}
	void set(uint index){_mask[index>>3]|=(1 << (index % 8));}
	void clear(uint index){_mask[index>>3] &= ~(1 << (index % 8));}
	bool get(uint index) const { return _mask[index>>3] & (1 << (index % 8)); }

};


class TreeNode:public ResourceObj {
public:
	uint value,freq;
	TreeNode* left, *right ;
	TreeNode(uint v,uint f):value(v),freq(f),left(nullptr),right(nullptr){}
};

class MinHeap:public ResourceObj {
private:
	TreeNode** _heap;//array
	uint _size;
	//maintain heap
	Compile* C;
	void heapify(uint i);
public:
	MinHeap(uint* data, uint* freqs,uint n,Compile* C);
	TreeNode* get_top();
	void insert(TreeNode* node);
	TreeNode* build_huffman_tree();


};


class Huffman:public ResourceObj {
private:
	Compile *C;
	TreeNode * _root=nullptr;
	MinHeap* _heap=nullptr;
	uint _huffmanCodeLen=0;
	struct Bitcode {
		uint len;
		ushort data;
	};
	Bitcode codes[256];//the maximum slots idx?
	void code_gen(TreeNode* root,uint len,ushort data);


public:
	Huffman(int* data, uint size, Compile* C);//
	uint encode();
	uint decode();


};


class HeuristicCSRGraph :public ResourceObj{
private:
	int* _offset;
	int* _edge;
	uint _nodeNum;
	uint _edgeNum;

public:
	HeuristicCSRGraph(int* offset, int* edge, int numNodes, int numEdges);
	~HeuristicCSRGraph();
	int* getOffset() const;
	int* getEdge() const;
	int getNodeNum() const;
	int getEdgeNum() const;
	void printGraph() const;
private:
	void optimizeIndices(int* oriOffset, int* oriEdge);
};










#endif //SONSERIALIZER_H
