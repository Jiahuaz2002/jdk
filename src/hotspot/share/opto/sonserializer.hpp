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


class SonSerializer:public ResourceObj{
private:
	Node* root=nullptr;
	outputStream*_output;
	char buffer[512];
	Compile* C;

public:

	SonSerializer(Compile* compile, const char* file_name=nullptr);
	~SonSerializer();
	void walk_nodes(Node* root, bool edges);
	void visit_node(Node* n,bool edges);
	void set_compile(Compile* compile) {C = compile; }
	void dump();

};



#endif //SONSERIALIZER_H
