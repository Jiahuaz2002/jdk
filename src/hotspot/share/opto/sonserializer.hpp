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
#include<opto/rootnode.hpp>
#include "opto/movenode.hpp"
#include "opto/locknode.hpp"
#include "opto/castnode.hpp"
#include "opto/convertnode.hpp"
#include "opto/intrinsicnode.hpp"
#include "opto/countbitsnode.hpp"
#include "opto/vectornode.hpp"
#include"gc/shenandoah/c2/shenandoahSupport.hpp"
#include "opto/memnode.hpp"
#include "opto/narrowptrnode.hpp"
#include "opto/arraycopynode.hpp"
#include "opto/mathexactnode.hpp"


#define NO_OUT_ARRAY ((Node**)-1)
class Graph;
class CSRGraph;
class Bitmask;
//__________________________________________________Control_____________________________________________________
class SonSerializer:public ResourceObj{
private:
	Compile* C;

	Node* _root=nullptr;
	char _buffer[512];
	uint _nodeNum=0;
	uint _edgeNum=0;
	Graph * _graph=nullptr;

public:
	void set_csr();
	SonSerializer(Compile* compile, const char* file_name=nullptr);

	~SonSerializer();
	void walk_nodes(Node* root);
	void set_compile(Compile* compile) {C = compile; }
	void compress_and_dump();
	bool deserialize();
};
//______________________________________________Graph Storage_________________________________________________
class Graph:public ResourceObj {
private:
public:
//	virtual void setCompressionStrategy()=0;
	virtual void compress_and_dump()=0;
	virtual bool deserialize()=0;

};
//CSR format
class CSRGraph:public Graph {
private:
	Node* _root=nullptr;
	int *_oriEdge=nullptr;//_oriEgde[_oriOffset[idx]]~_oriEdge[_oriOffset[lowest upper bound of idx]] is the outgoing edges of idx.

	int *_offset=nullptr;//after index reassign.
	int *_edge=nullptr;//for current phase, just for validation.

	int *_edgeIdx=nullptr;//slot index
	Bitmask* _edgeIdxMask;//
	uint _edgeIdxSize;//actual num of the stored idx

	int *_idHash=nullptr;//size=_nodeNum, _idxHash[newIdx]=oldIdx.


	uint _nodeNum;
	uint _edgeNum;//the edge num = _actlEdgeNum + -1( the empty slot)
	uint _actlEdgeNum;//the actual valid edgenum


	Compile* C;

	uint _kbitBytesLen;//_after kbit-encoding the Bytes length of the _offset. _kbitBytesLen-1 is the final index.

	ushort _vptrNum=0;
	fileStream* _f;


public:

	CSRGraph(Node* nd,uint nodeNumber,uint edgeNumber,Compile* C);
	void compress_and_dump()override;
	bool deserialize()override;
	~CSRGraph();
	ResourceHashtable<int,int>* _nodeBytes;//the dictionary of node size
	void* _vptrTable[512]={};// the dictionary of the vtpr

private:
	//helping functions
	int find_lowest_upper_bound(  int num,  bool equal)const ;
	int lookup_idx_hash( int old)const ;
	void set_bit(u_int8_t* obj, int bit);//bit from 0->7, set bit from 0->1
	bool preliminary_known_node(Node* node);
	void initialize_nodebyte();
	int kbit_encoding_pos(int*a,int sz);//only consider positive value
	void kbit_decoding_pos(int*a,int sz);
	int kbit_encoding(int* a,int sz);
	void kbit_decoding(int* a,int sz);
	int bit4_encoding(int* a,int sz);
	void bit4_decoding(int* a,int sz);
	void set_vptr_table(Node* root);
	void store_node(Node* n,fileStream* f);
	void load_node(Node* n,fileStream* f,int sz,ushort op);
	void store_offset();
	void store_edgeIdx();
	void store_edge();
	void store_sorted_edge();
	void store_partly_sorted_edge();


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


#endif //SONSERIALIZER_H