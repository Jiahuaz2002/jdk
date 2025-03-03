//
// Created by wlr on 2/19/25.
//
#include "opto/sonserializer.hpp"

SonSerializer::SonSerializer(Compile* compile,const char* file_name){
	_output = new (mtCompiler) fileStream(file_name,"w");
	root = (Node*)compile->root();
  this->set_compile((compile));
	
}

SonSerializer:: ~SonSerializer()
{
	if (root) delete root;
	if(_output) delete _output;
	
}

void SonSerializer:: dump(){
  walk_nodes(root,1);
	return;
}

void SonSerializer::walk_nodes(Node* start, bool edges) {
  VectorSet visited;
  GrowableArray<Node *> nodeStack(Thread::current()->resource_area(), 0, 0, nullptr);
  nodeStack.push(start);

  while (nodeStack.length() > 0) {
    Node* n = nodeStack.pop();
    if (visited.test_set(n->_idx)) {
      continue;
    }

    visit_node(n, edges);
    bool  _traverse_outs=true;
    if (_traverse_outs) {//default true
      for (DUIterator i = n->outs(); n->has_out(i); i++) {
        nodeStack.push(n->out(i));
      }
    }

    for (uint i = 0; i < n->len(); i++) {
      if (n->in(i) != nullptr) {
        nodeStack.push(n->in(i));
      }
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

  Compile::current()->_in_dump_cnt++;
  _output->print_cr("Node Name:%s",(const char *)node->Name());
  const Type *t = node->bottom_type();
  _output->print_cr("Node type:%s",(const char *)t->msg());
  if (t->category() != Type::Category::Control &&
      t->category() != Type::Category::Memory) {
    // Print detailed type information for nodes whose type is not trivial.
    buffer[0] = 0;
    stringStream bottom_type_stream(buffer, sizeof(buffer) - 1);
    t->dump_on(&bottom_type_stream);
    _output->print_cr("bottom_type:%s",buffer);
    if (C->types() != nullptr && C->matcher() == nullptr) {
      // Phase types maintained during optimization (GVN, IGVN, CCP) are
      // available and valid (not in code generation phase).
      const Type* pt = (*C->types())[node->_idx];
      if (pt != nullptr) {
        buffer[0] = 0;
        stringStream phase_type_stream(buffer, sizeof(buffer) - 1);
        pt->dump_on(&phase_type_stream);
        _output->print_cr("phase_type:%s",buffer);
      }
    }
  }

  switch (t->category()) {
    case Type::Category::Data:
      _output->print_cr("category: data");
    break;
    case Type::Category::Memory:
      _output->print_cr("category: memory");
    break;
    case Type::Category::Mixed:
      _output->print_cr("category: mixed");
    break;
    case Type::Category::Control:
      _output->print_cr("category: control");
    break;
    case Type::Category::Other:
      _output->print_cr("category: other");
    break;
    case Type::Category::Undef:
      _output->print_cr("category: undef");
    break;
  }

 // Node_Notes* nn = C->node_notes_at(node->_idx);


  const jushort flags = node->flags();
  if (flags & Node::Flag_is_Copy) {
    _output->print_cr("is_copy: true");
  }
  if (flags & Node::Flag_rematerialize) {
    _output->print_cr("rematerialize: true");
  }
  if (flags & Node::Flag_needs_anti_dependence_check) {
    _output->print_cr("needs_anti_dependence_check: true");
  }
  if (flags & Node::Flag_is_macro) {
    _output->print_cr("is_macro: true");
  }
  if (flags & Node::Flag_is_Con) {
    _output->print_cr("is_con: true");
  }
  if (flags & Node::Flag_is_cisc_alternate) {
    _output->print_cr("is_cisc_alternate: true");
  }
  if (flags & Node::Flag_is_dead_loop_safe) {
    _output->print_cr("is_dead_loop_safe: true");
  }
  if (flags & Node::Flag_may_be_short_branch) {
    _output->print_cr("may_be_short_branch: true");
  }
  if (flags & Node::Flag_has_call) {
    _output->print_cr("has_call: true");
  }
  if (flags & Node::Flag_has_swapped_edges) {
    _output->print_cr("has_swapped_edges: true");
  }


  if (C->matcher() != nullptr) {
    if (C->matcher()->is_shared(node)) {
      _output->print_cr("is_shared: true");
    } else {
      _output->print_cr("is_shared: false");
    }

    if (C->matcher()->is_dontcare(node)) {
      _output->print_cr("is_dontcare: true");
    } else {
      _output->print_cr("is_dontcare: false");
    }

    Node* old = C->matcher()->find_old_node(node);
    if (old != nullptr) {
      _output->print_cr("old_node_idx: %d", old->_idx);
    }
  }

  if (node->is_Proj()) {
    _output->print_cr("con: %d", (int)node->as_Proj()->_con);
  }

  if (node->is_Mach()) {
    _output->print_cr("idealOpcode: %s", (const char *)NodeClassNames[node->as_Mach()->ideal_Opcode()]);
  }


  buffer[0] = 0;
  stringStream s2(buffer, sizeof(buffer) - 1);

  node->dump_spec(&s2);
  if (t != nullptr && (t->isa_instptr() || t->isa_instklassptr())) {
    const TypeInstPtr  *toop = t->isa_instptr();
    const TypeInstKlassPtr *tkls = t->isa_instklassptr();
    if (toop) {
      s2.print("  Oop:");
    } else if (tkls) {
      s2.print("  Klass:");
    }
    t->dump_on(&s2);
  } else if( t == Type::MEMORY ) {
    s2.print("  Memory:");
    MemNode::dump_adr_type(node, node->adr_type(), &s2);
  }

  assert(s2.size() < sizeof(buffer), "size in range");
  _output->print_cr("dump_spec: %s", buffer);

  if (node->is_block_proj()) {
    _output->print_cr("is_block_proj: true");
  }

  if (node->is_block_start()) {
    _output->print_cr("is_block_start: true");
  }

  const char *short_name = "short_name";
  if (strcmp(node->Name(), "Parm") == 0 && node->as_Proj()->_con >= TypeFunc::Parms) {
    int index = node->as_Proj()->_con - TypeFunc::Parms;
    if (index >= 10) {
      _output->print_cr("%s: PA", short_name);
    } else {
      os::snprintf_checked(buffer, sizeof(buffer), "P%d", index);
      _output->print_cr("%s: %s", short_name, buffer);
    }
  } else if (strcmp(node->Name(), "IfTrue") == 0) {
    _output->print_cr("%s: T", short_name);
  } else if (strcmp(node->Name(), "IfFalse") == 0) {
    _output->print_cr("%s: F", short_name);
  } else if ((node->is_Con() && node->is_Type()) || node->is_Proj()) {

    if (t->base() == Type::Int && t->is_int()->is_con()) {
      const TypeInt *typeInt = t->is_int();
      assert(typeInt->is_con(), "must be constant");
      jint value = typeInt->get_con();

      // max. 2 chars allowed
      if (value >= -9 && value <= 99) {
        os::snprintf_checked(buffer, sizeof(buffer), "%d", value);
        _output->print_cr("%s: %s", short_name, buffer);
      } else {
        _output->print_cr("%s: I", short_name);
      }
    } else if (t == Type::TOP) {
      _output->print_cr("%s: ^", short_name);
    } else if (t->base() == Type::Long && t->is_long()->is_con()) {
      const TypeLong *typeLong = t->is_long();
      assert(typeLong->is_con(), "must be constant");
      jlong value = typeLong->get_con();

      // max. 2 chars allowed
      if (value >= -9 && value <= 99) {
        os::snprintf_checked(buffer, sizeof(buffer), JLONG_FORMAT, value);
        _output->print_cr("%s: %s", short_name, buffer);
      } else {
        _output->print_cr("%s: L", short_name);
      }
    } else if (t->base() == Type::KlassPtr || t->base() == Type::InstKlassPtr || t->base() == Type::AryKlassPtr) {
      _output->print_cr("%s: CP", short_name);
    } else if (t->base() == Type::Control) {
      _output->print_cr("%s: C", short_name);
    } else if (t->base() == Type::Memory) {
      _output->print_cr("%s: M", short_name);
    } else if (t->base() == Type::Abio) {
      _output->print_cr("%s: IO", short_name);
    } else if (t->base() == Type::Return_Address) {
      _output->print_cr("%s: RA", short_name);
    } else if (t->base() == Type::AnyPtr) {
      _output->print_cr("%s: P", short_name);
    } else if (t->base() == Type::RawPtr) {
      _output->print_cr("%s: RP", short_name);
    } else if (t->base() == Type::AryPtr) {
      _output->print_cr("%s: AP", short_name);
    }

    Compile::current()->_in_dump_cnt--;
  }
}



