#pragma once

#include <assert.h>
#include "MtModLibrary.h"
#include "MtModule.h"
#include "MtIterator.h"
#include <set>
#include <string.h>

//------------------------------------------------------------------------------

inline bool operator < (const TSNode& a, const TSNode& b) {
  return memcmp(&a, &b, sizeof(TSNode)) < 0;
}

//------------------------------------------------------------------------------

struct MtCursor {

  MtCursor(MtModLibrary* mod_lib, MtModule* mod, FILE* out)
    : mod_lib(mod_lib), mod(mod), out(out) {
    indent_stack.push_back("");
    cursor = mod->source;
  }

  MtModLibrary* mod_lib;
  MtModule* mod;
  FILE* out = nullptr;
  const char* cursor = nullptr;
  std::vector<std::string> indent_stack;

  bool in_init = false;
  bool in_comb = false;
  bool in_seq = false;
  bool in_final = false;
  TSNode current_function_name = { 0 };

  void push_indent(MtHandle n) {
    if (ts_node_is_null(n)) {
      indent_stack.push_back(indent_stack.back());
    }
    else {
      auto e = mod->start(n);
      auto b = e;
      while (*b != '\n') b--;
      indent_stack.push_back(std::string(b + 1, e));
    }
  }

  void pop_indent() {
    indent_stack.pop_back();
  }

  void emit_newline() {
    emit("\n%s", indent_stack.back().c_str());
  }

  void check_dirty_tick(MtHandle n);
  void check_dirty_tick_dispatch(MtHandle n, std::set<TSNode>& dirty_fields, int depth);

  void check_dirty_read(MtHandle n, std::set<TSNode>& dirty_fields, int depth);
  void check_dirty_write(MtHandle n, std::set<TSNode>& dirty_fields, int depth);
  void check_dirty_if(MtHandle n, std::set<TSNode>& dirty_fields, int depth);
  void check_dirty_call(MtHandle n, std::set<TSNode>& dirty_fields, int depth);
  // FIXME add case

  void check_dirty_tock(MtHandle n);
  void check_dirty_tock_dispatch(MtHandle n, std::set<TSNode>& dirty_fields);

  void dump_node_line(MtHandle n);

  void print_error(MtHandle n, const char* fmt, ...);

  //----------

  bool match(MtHandle n, const char* str) { return mod->match(n, str); }

  void emit_span(const char* a, const char* b);
  void emit(MtHandle n);
  void emit(const char* fmt, ...);
  void emit_replacement(MtHandle n, const char* fmt, ...);
  void skip_over(MtHandle n);
  void skip_whitespace();
  void advance_to(MtHandle n);
  void comment_out(MtHandle n);

  //----------

  void emit_number_literal(MtHandle n);
  void emit_primitive_type(MtHandle n);
  void emit_type_identifier(MtHandle n);
  void emit_preproc_include(MtHandle n);
  void emit_return_statement(MtHandle n);
  void emit_assignment_expression(MtHandle n);
  void emit_call_expression(MtHandle n);
  void emit_function_definition(MtHandle n);
  void emit_glue_declaration(MtHandle decl, const std::string& prefix);
  void emit_field_declaration(MtHandle decl);
  void emit_class_specifier(MtHandle n);
  void emit_compound_statement(MtHandle n);
  void emit_template_type(MtHandle n);
  void emit_module_parameters(MtHandle n);
  void emit_template_declaration(MtHandle n);
  void emit_template_argument_list(MtHandle n);
  void emit_enumerator_list(MtHandle n);
  void emit_translation_unit(MtHandle n);
  void emit_flat_field_expression(MtHandle n);
  void emit_dispatch(MtHandle n);

  void emit_hoisted_decls(MtHandle n);


  void emit_init_declarator_as_decl(MtHandle n);
  void emit_init_declarator_as_assign(MtHandle n);
};

//------------------------------------------------------------------------------
