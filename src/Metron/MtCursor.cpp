#include "MtCursor.h"
#include "MtModLibrary.h"
#include "MtModule.h"
#include <stdarg.h>
#include <regex>
#include <assert.h>

#include "Platform.h"
#include "MtIterator.h"
#include "../Plait/TreeSymbols.h"

//------------------------------------------------------------------------------

void MtCursor::check_dirty_tick(TSNode func_def) {
  std::set<TSNode> dirty_fields;
  check_dirty_tick_dispatch(func_def, dirty_fields, 0);
}

//------------------------------------------------------------------------------

void MtCursor::dump_node_line(TSNode n) {
  auto start = &mod->source[ts_node_start_byte(n)];

  auto a = start;
  auto b = start;
  while (a > mod->source     && *a != '\n' && *a != '\r') a--;
  while (b < mod->source_end && *b != '\n' && *b != '\r') b++;

  if (*a == '\n' || *a == '\r') a++;

  while (a != b) {
    fputc(*a++, stdout);
  }
}

//------------------------------------------------------------------------------

void MtCursor::print_error(TSNode n, const char* fmt, ...) {

  emit("\n########################################\n");

  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  emit("@%04d: ", ts_node_start_point(n).row + 1);
  dump_node_line(n);
  printf("\n");

  emit("halting...\n");
  emit("########################################\n");
  debugbreak();
}

//------------------------------------------------------------------------------

void MtCursor::check_dirty_tick_dispatch(TSNode n, std::set<TSNode>& dirty_fields, int depth) {
  if (ts_node_is_null(n)) return;
  if (!ts_node_is_named(n)) return;

  switch (ts_node_symbol(n)) {

  case sym_number_literal:
  case sym_template_type:
  case sym_primitive_type:
  case sym_function_declarator:
    break;

  case sym_comment:
  case sym_subscript_expression:
  case sym_parenthesized_expression:
  case sym_init_declarator:
  case sym_declaration:
  case sym_binary_expression:
  case sym_unary_expression:
  case sym_condition_clause:
  case sym_compound_statement:
  case sym_expression_statement:
  case sym_argument_list:
  case sym_function_definition:
  case sym_return_statement:
    for (auto c : n) check_dirty_tick_dispatch(c, dirty_fields, depth + 1);
    break;

  case sym_assignment_expression: {
    auto lhs = ts_node_child_by_field_id(n, field_left);
    auto rhs = ts_node_child_by_field_id(n, field_right);

    check_dirty_tick_dispatch(rhs, dirty_fields, depth + 1);

    auto field = mod->get_field_by_id(lhs);
    if (!ts_node_is_null(field)) {
      if (dirty_fields.contains(field)) {
        print_error(n, "wrote dirty field - %s\n", mod->node_to_name(field).c_str());
      }
      dirty_fields.insert(field);
    }
    break;
  }

  case sym_identifier: {
    auto field = mod->get_field_by_id(n);
    if (!ts_node_is_null(field)) {
      if (dirty_fields.contains(field)) {
        print_error(n, "read dirty field - %s\n", mod->node_to_name(field).c_str());
      }
    }
    break;
  }

  case sym_if_statement: {
    check_dirty_tick_dispatch(ts_node_child_by_field_id(n, field_condition), dirty_fields, depth + 1);

    std::set<TSNode> if_set = dirty_fields;
    std::set<TSNode> else_set = dirty_fields;

    check_dirty_tick_dispatch(ts_node_child_by_field_id(n, field_consequence), if_set, depth + 1);
    check_dirty_tick_dispatch(ts_node_child_by_field_id(n, field_alternative), else_set, depth + 1);

    dirty_fields.merge(if_set);
    dirty_fields.merge(else_set);
    break;
  }

  case sym_call_expression: {
    auto node_func = ts_node_child_by_field_id(n, field_function);
    auto node_args = ts_node_child_by_field_id(n, field_arguments);

    if (ts_node_symbol(node_func) == sym_identifier) {
      // local function call, traverse args and then function body
      check_dirty_tick_dispatch(node_args, dirty_fields, depth + 1);

      auto task = mod->get_task_by_id(node_func);
      if (!ts_node_is_null(task)) check_dirty_tick_dispatch(task, dirty_fields, depth + 1);

      auto func = mod->get_function_by_id(node_func);
      if (!ts_node_is_null(func)) check_dirty_tick_dispatch(func, dirty_fields, depth + 1);
    }

    break;
  }

  default:
    printf("\n");
    printf("#####\n");
    printf("unknown node\n");
    mod->dump_tree(n, 3);
    debugbreak();
  }
}

//------------------------------------------------------------------------------
// Generic emit() methods

void MtCursor::emit_span(const char* a, const char* b) {
  assert(cursor >= mod->source);
  assert(cursor <= mod->source_end);
  if (out) fwrite(a, 1, b - a, out);
  fwrite(a, 1, b - a, stdout);
}

void MtCursor::emit(TSNode n) {
  emit_span(cursor, mod->end(n));
  cursor = mod->end(n);
}

void MtCursor::emit(const char* fmt, ...) {
  {
    va_list args;
    va_start(args, fmt);
    if (out) vfprintf(out, fmt, args);
    va_end(args);
  }
  {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
  }
}

void MtCursor::emit_replacement(TSNode n, const char* fmt, ...) {
  advance_to(n);
  {
    va_list args;
    va_start(args, fmt);
    if (out) vfprintf(out, fmt, args);
    va_end(args);
  }
  {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
  }
  cursor = mod->end(n);
}

void MtCursor::skip_over(TSNode n) {
  advance_to(n);
  cursor = mod->end(n);
}

void MtCursor::skip_whitespace() {
  while(*cursor && isspace(*cursor)) {
    cursor++;
  }
}

void MtCursor::advance_to(TSNode n) {
  assert(cursor <= mod->start(n));
  emit_span(cursor, mod->start(n));
  cursor = mod->start(n);
}

void MtCursor::comment_out(TSNode n) {
  advance_to(n);
  emit("/*");
  emit(n);
  emit("*/");
}

//------------------------------------------------------------------------------
// Replace "#include" with "`include" and ".h" with ".sv"

void MtCursor::emit_preproc_include(TSNode n) {
  for (auto c : n) {
    switch (c.sym) {
    case aux_sym_preproc_include_token1: emit_replacement(c, "`include"); break;
    case sym_string_literal: {
      auto path = mod->body(c);
      path.pop_back();
      path.append(".sv\"");
      emit_replacement(c, "%s", path.c_str());
      break;
    }
    default: emit_dispatch(c); break;
    }
  }
}

//------------------------------------------------------------------------------
// Change '=' to '<=' if lhs is a field and we're inside a sequential block.

void MtCursor::emit_assignment_expression(TSNode n) {
  auto lhs = ts_node_child_by_field_id(n, field_left);
  auto op  = ts_node_child_by_field_id(n, field_operator);
  auto rhs = ts_node_child_by_field_id(n, field_right);

  bool lhs_is_field = false;
  if (ts_node_symbol(lhs) == sym_identifier) {
    std::string lhs_name = mod->body(lhs);
    for (const auto& f : mod->fields) {
      if (mod->node_to_name(f) == lhs_name) {
        lhs_is_field = true;
        break;
      }
    }
  }

  emit_dispatch(lhs);

  advance_to(op);
  if (in_seq && lhs_is_field) emit("<");
  emit_dispatch(op);

  emit_dispatch(rhs);
}

//------------------------------------------------------------------------------
// Replace function names with macro names where needed, comment out explicit
// init/final/tick/tock calls.

void MtCursor::emit_call_expression(TSNode n) {
  auto call_func = ts_node_child_by_field_id(n, field_function);
  auto call_args = ts_node_child_by_field_id(n, field_arguments);

  // If we're calling a member function, look at the name of the member
  // function and not the whole foo.bar().
  if (ts_node_symbol(call_func) == sym_field_expression) {
    call_func = ts_node_child_by_field_id(call_func, field_field);
  }

  if (mod->match(call_func, "clog2")) {
    emit_replacement(call_func, "$clog2");
    emit_dispatch(call_args);
  }
  else if (mod->match(call_func, "readmemh")) {
    emit_replacement(call_func, "$readmemh");
    emit_dispatch(call_args);
  }
  else if (mod->match(call_func, "printf")) {
    emit_replacement(call_func, "$write");
    emit_dispatch(call_args);
  }
  else if (mod->match(call_func, "init")) {
    comment_out(n);
  }
  else if (mod->match(call_func, "final")) {
    comment_out(n);
  }
  else if (mod->match(call_func, "tick")) {
    comment_out(n);
  }
  else if (mod->match(call_func, "tock")) {
    comment_out(n);
  }
  else {
    // All other function/task calls go through normally.
    for (auto c : n) emit_dispatch(c);
  }
}

//------------------------------------------------------------------------------
// Replace "logic blah = x;" with "logic blah;"

void MtCursor::emit_init_declarator_as_decl(TSNode n) {

  for (auto c : n) {
    switch (c.field) {

    case field_type:
      emit_dispatch(c);
      break;

    case field_declarator:
      for (auto gc : c) {
        switch (gc.field) {
        case field_declarator:
          emit(gc);
          skip_whitespace();
          break;
        default:
          skip_over(gc);
          skip_whitespace();
          break;
        }
      }
      break;

    default:
      emit_dispatch(c);
      break;
    }
  }
}

//------------------------------------------------------------------------------
// Replace "logic blah = x;" with "blah = x;"

void MtCursor::emit_init_declarator_as_assign(TSNode n) {

  auto node_decl = ts_node_child_by_field_id(n, field_declarator);
  auto decl_type = ts_node_symbol(node_decl);

  if (decl_type == sym_init_declarator) {
    for (auto c : n) {
      switch (c.field) {
      case field_type:
        skip_over(c);
        skip_whitespace();
        break;
      default:
        emit_dispatch(c);
        break;
      }
    }
  }
  else {
    skip_over(n);
    skip_whitespace();
  }
}

//------------------------------------------------------------------------------
// Emit local variable declarations at the top of the block scope.

void MtCursor::emit_hoisted_decls(TSNode n) {
  MtCursor old_cursor = *this;
  for (auto c : n) {
    if (c.sym == sym_declaration) {
      cursor = mod->start(c);
      emit_newline();
      emit_init_declarator_as_decl(c);
    }
  }
  *this = old_cursor;
}

//------------------------------------------------------------------------------
// Change "init/tick/tock" to "initial begin / always_comb / always_ff", change
// void methods to tasks, and change const methods to functions.

void MtCursor::emit_function_definition(TSNode func_def) {

  assert(ts_node_child_count(func_def) == 3);
  assert(ts_node_field_id_for_child(func_def, 0) == field_type);
  assert(ts_node_field_id_for_child(func_def, 1) == field_declarator);
  assert(ts_node_field_id_for_child(func_def, 2) == field_body);

  auto func_type = ts_node_child_by_field_id(func_def, field_type);
  auto func_decl = ts_node_child_by_field_id(func_def, field_declarator);
  auto func_body = ts_node_child_by_field_id(func_def, field_body);

  bool is_task = false;
  bool is_init = false;
  bool is_tock = false;
  bool is_tick = false;

  //----------

  is_task = mod->match(func_type, "void");
  skip_over(func_type);
  skip_whitespace();

  //----------

  in_init = false;
  in_comb = false;
  in_seq = false;

  current_function_name = ts_node_child_by_field_id(func_decl, field_declarator);
  is_init = is_task && mod->match(current_function_name, "init");
  is_tick = is_task && mod->match(current_function_name, "tick");
  is_tock = is_task && mod->match(current_function_name, "tock");

  if (is_init) {
    emit_replacement(func_decl, "initial");
    in_init = true;
  }
  else if (is_tick) {
    emit_replacement(func_decl, "always_ff @(posedge clk, negedge rst_n)");
    in_seq = true;
  }
  else if (is_tock) {
    emit_replacement(func_decl, "always_comb");
    in_comb = true;
  }
  else {
    advance_to(func_decl);
    if (is_task) {
      emit("task ");
    }
    else {
      emit("function %s ", mod->body(func_type).c_str());
    }

    emit_dispatch(func_decl);
    skip_whitespace();
    emit(";");

    in_seq = is_task;
    in_comb = !is_task;
  }

  //----------
  // Verify that tick() obeys the read-before-write rule.

  if (is_tick) {
    check_dirty_tick(func_def);
  }

  //----------
  // Emit the module body with the correct type of "begin/end" pair,
  // hoisting locals to the top of the body scope.

  push_indent(ts_node_named_child(func_body, 0));

  for (auto c : func_body) {
    switch (c.sym) {
    case anon_sym_LBRACE: {
      if      (is_init) emit_replacement(c, "begin : INIT");
      else if (is_tick) emit_replacement(c, "begin : TICK");
      else if (is_tock) emit_replacement(c, "begin : TOCK");
      else if (is_task) emit_replacement(c, "");
      else              emit_replacement(c, "");

      emit_hoisted_decls(func_body);
      break;
    }

    case sym_declaration: {
      emit_init_declarator_as_assign(c);
      break;
    }

    case anon_sym_RBRACE:
      if      (is_init) emit_replacement(c, "end");
      else if (is_tick) emit_replacement(c, "end");
      else if (is_tock) emit_replacement(c, "end");
      else if (is_task) emit_replacement(c, "endtask");
      else              emit_replacement(c, "endfunction");
      break;

    default:
      emit_dispatch(c);
      break;
    }
  }

  pop_indent();

  //----------
  // For each call to {submodule}.tick() in module::tick(), emit glue assignments.

  current_function_name = { 0 };
  in_init = false;
  in_comb = false;
  in_seq  = false;

  if (is_tick && !mod->submodules.empty()) {
    emit_newline();

    std::vector<TSNode> submod_call_nodes;

    mod->visit_tree(func_def, [&](TSNode child) {
      auto sym = ts_node_symbol(child);
      if (sym == sym_call_expression) {
        auto call_func = ts_node_child_by_field_id(child, field_function);

        if (ts_node_symbol(call_func) == sym_identifier) {
          // not a submod call
        }
        else {
          auto call_args = ts_node_child_by_field_id(child, field_arguments);
          auto call_this = ts_node_child_by_field_id(call_func, field_argument);
          auto func_name = ts_node_child_by_field_id(call_func, field_field);
          if (mod->match(func_name, "tick")) {
            submod_call_nodes.push_back(child);
          }
        }
      }
    });

    for (auto& submod_call : submod_call_nodes) {
      auto call_func = ts_node_child_by_field_id(submod_call, field_function);
      auto call_args = ts_node_child_by_field_id(submod_call, field_arguments);
      auto call_this = ts_node_child_by_field_id(call_func, field_argument);
      auto func_name = ts_node_child_by_field_id(call_func, field_field);

      for (auto& sm : mod->submodules) {
        auto submod_type = mod->node_to_type(sm);
        auto submod_name = mod->node_to_name(sm);
        if (submod_name == mod->node_to_name(call_this)) {
          auto submod = mod_lib->find_module(submod_type);

          std::vector<std::string> call_src;
          std::vector<std::string> call_dst;

          for (auto arg : call_args) {
            if (!ts_node_is_named(arg)) continue;
            auto src = mod->node_to_name(arg);
            for (auto& c : src) if (c == '.') c = '_';
            if (src != "rst_n") call_src.push_back(src);
          }

          for (auto& input : submod->inputs) {
            call_dst.push_back(submod->node_to_name(input));
          }

          assert(call_src.size() == call_dst.size());

          for (int i = 0; i < call_src.size(); i++) {
            
            emit_newline();
            emit("assign %s_%s = %s;",
              submod_name.c_str(),
              call_dst[i].c_str(),
              call_src[i].c_str());
          }
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
// Emit "<type> <submod_name>_<param_name>;" glue declarations because we can't
// directly pass arguments to submodules.

void MtCursor::emit_glue_declaration(TSNode decl, const std::string& prefix) {

  assert((ts_node_symbol(decl) == sym_field_declaration) ||
         (ts_node_symbol(decl) == sym_parameter_declaration));

  auto node_type = ts_node_child_by_field_id(decl, field_type);
  auto type_sym  = ts_node_symbol(node_type);
  auto node_name = ts_node_child_by_field_id(decl, field_declarator);

  std::string type_name;

  if (type_sym == alias_sym_type_identifier || type_sym == sym_primitive_type) {
    type_name = mod->body(node_type);
  }
  else if (type_sym == sym_template_type) {
    type_name = mod->body(ts_node_child_by_field_id(node_type, field_name));
  }
  else {
    debugbreak();
  }

  emit_dispatch(node_type);
  advance_to(node_name);
  emit("%s_", prefix.c_str());
  emit_dispatch(node_name);
  emit(";");
  emit_newline();
}

//------------------------------------------------------------------------------
// Emit field declarations. For submodules, also emit glue declarations and
// append the glue parameter list to the field.

void MtCursor::emit_field_declaration(TSNode decl) {

  // Check if this field is a submodule by looking up its type name in our
  // module list.

  auto node_type = ts_node_child_by_field_id(decl, field_type);
  std::string type_name;

  switch (ts_node_symbol(node_type)) {
  case alias_sym_type_identifier: type_name = mod->body(node_type); break;
  case sym_primitive_type:        type_name = mod->body(node_type); break;
  case sym_template_type:         type_name = mod->body(ts_node_child_by_field_id(node_type, field_name)); break;
  default:                        debugbreak();
  }

  auto submod = mod_lib->find_module(type_name);


  // If this isn't a submodule, just tack on "input" and "output" annotations.
  if (!submod) {
    if (mod->field_is_input(decl)) {
      advance_to(decl);
      emit("input ");
    }
    if (mod->field_is_output(decl)) {
      advance_to(decl);
      emit("output ");
    }
    for (auto c : decl) emit_dispatch(c);
    return;
  }

  // If this is a submodule, emit glue parameters and patch the glue parameter
  // list into the submodule declaration.

  advance_to(decl);
  std::string inst_name = mod->node_to_name(decl);

  for (auto& input : submod->inputs) {
    MtCursor sub_cursor(mod_lib, submod, out);
    sub_cursor.cursor = submod->start(input);
    sub_cursor.indent_stack = indent_stack;
    sub_cursor.emit_glue_declaration(input, inst_name);
  }

  for (auto& output : submod->outputs) {
    MtCursor sub_cursor(mod_lib, submod, out);
    sub_cursor.cursor = submod->start(output);
    sub_cursor.indent_stack = indent_stack;
    sub_cursor.emit_glue_declaration(output, inst_name);
  }

  for (auto c : decl) {
    emit_dispatch(c);
    if (c.field == field_declarator) {
      emit("(clk, rst_n");
      for (auto& input : submod->inputs) {
        emit(", %s_%s", inst_name.c_str(), submod->node_to_name(input).c_str());
      }
      for (auto& output : submod->outputs) {
        emit(", %s_%s", inst_name.c_str(), submod->node_to_name(output).c_str());
      }
      emit(")");
    }
  }

  emit_newline();
}

//------------------------------------------------------------------------------
// Change class/struct to module, add default clk/rst inputs, add input and
// ouptut ports to module param list.

void MtCursor::emit_class_specifier(TSNode n) {
  for (auto c : n) {
    if (c.sym == anon_sym_class || c.sym == anon_sym_struct) {
      emit_replacement(c, "module");
    }
    else if (c.field == field_name) {
      emit_dispatch(c);

      // Patch the template parameter list in after the module declaration
      if (!ts_node_is_null(mod->module_param_list)) {
        emit_newline();
        MtCursor sub_cursor = *this;
        sub_cursor.cursor = mod->start(mod->module_param_list);
        sub_cursor.emit_module_parameters(mod->module_param_list);
      }

      // Emit an old-style port list
      emit_newline();
      emit("(clk, rst_n");
      for (auto input : mod->inputs) {
        emit(", %s", mod->node_to_name(input).c_str());
      }
      for (auto output : mod->outputs) {
        emit(", %s", mod->node_to_name(output).c_str());
      }
      emit(");");

    }
    else if (c.field == field_body) {
      // And the declaration of the ports will be in the module body along with
      // the rest of the module.

      push_indent(ts_node_named_child(c, 0));

      emit_newline();
      emit("/*verilator public_module*/");
      emit_newline();

      emit_newline();
      emit("input logic clk;");

      emit_newline();
      emit("input logic rst_n;");

      for (auto input : mod->inputs) {
        MtCursor sub_cursor = *this;
        sub_cursor.cursor = mod->start(input);
        emit_newline();
        emit("input ");
        sub_cursor.emit_dispatch(input);
        emit(";");
      }

      // Emit the module body, with a few modifications.
      for (auto gc : c) {
        switch (gc.sym) {
        // Discard the opening brace
        case anon_sym_LBRACE: {
          emit_replacement(gc, "");
          break;
        }
        // Replace the closing brace with "endmodule"
        case anon_sym_RBRACE: {
          emit_replacement(gc, "endmodule");
          break;
        }
        // Discard the seimcolon at the end of class{};"
        case anon_sym_SEMI: {
          emit_replacement(gc, "");
          break;
        }

        default: {
          emit_dispatch(gc);
          break;
        }
        }
      }

      pop_indent();
    }
    else {
      debugbreak();
    }
  }
}

//------------------------------------------------------------------------------
// Change "{ blah(); }" to "begin blah(); end"

void MtCursor::emit_compound_statement(TSNode body) {
  push_indent(ts_node_named_child(body, 0));

  for (auto c : body) {
    switch (c.sym) {
    case anon_sym_LBRACE: {
      emit_replacement(c, "begin");
      emit_hoisted_decls(body);
      break;
    }

    case sym_declaration: {
      emit_init_declarator_as_assign(c);
      break;
    }

    case anon_sym_RBRACE:
      emit_replacement(c, "end");
      break;

    default: {
      emit_dispatch(c);
      break;
    }
    }
  }

  pop_indent();
}

//------------------------------------------------------------------------------
// Change logic<N> to logic[N-1:0]

void MtCursor::emit_template_type(TSNode n) {
  //mod->dump_tree(n);

  auto node_name = ts_node_child_by_field_id(n, field_name);
  auto node_args = ts_node_child_by_field_id(n, field_arguments);

  bool is_logic = mod->match(node_name, "logic");

  if (!is_logic) {
    emit_dispatch(node_name);
    emit_dispatch(node_args);
    return;
  }

  emit_dispatch(node_name);
  for (auto c : node_args) {
    switch (c.sym) {
    case anon_sym_LT: skip_over(c); break;
    case anon_sym_GT: skip_over(c); break;
    case sym_number_literal: {
      int width = atoi(mod->start(c));
      if (width > 1) emit("[%d:0]", width - 1);
      skip_over(c);
      break;
    }
    case sym_type_descriptor: {
      emit("[");
      emit(c);
      emit("-1:0]");
      break;
    }
    default: debugbreak();
    }
  }
}

//------------------------------------------------------------------------------
// Change (template)<int param, int param> to
// #(parameter int param, parameter int param)

void MtCursor::emit_module_parameters(TSNode n) {
  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_LT: emit_replacement(c, "#("); break;
    case anon_sym_GT: emit_replacement(c, ")"); break;

    // intentional fallthrough, we're just appending "parameter "
    case sym_parameter_declaration:
    case sym_optional_parameter_declaration:
      advance_to(c);
      emit("parameter ");
    default:
      emit_dispatch(c);
      break;
    }
  }
}

//------------------------------------------------------------------------------
// Change <param, param> to #(param, param)

void MtCursor::emit_template_argument_list(TSNode n) {
  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_LT: emit_replacement(c, " #("); break;
    case anon_sym_GT: emit_replacement(c, ")"); break;
    default:          emit_dispatch(c); break;
    }
  }
}

//------------------------------------------------------------------------------
// Enum lists do _not_ turn braces into begin/end.

void MtCursor::emit_enumerator_list(TSNode n) {
  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_LBRACE: emit(c); break;
    case anon_sym_RBRACE: emit(c); break;
    default:              emit_dispatch(c); break;
    }
  }
}

//------------------------------------------------------------------------------
// Discard any trailing semicolons in the translation unit.

void MtCursor::emit_translation_unit(TSNode n) {

  emit("/* verilator lint_off WIDTH */\n");
  emit("`default_nettype none\n");

  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_SEMI: skip_over(c); break;
    default:            emit_dispatch(c); break;
    }
  }

  emit_span(cursor, mod->source_end);
}

//------------------------------------------------------------------------------
// Replace "0x" prefixes with "'h"
// Replace "0b" prefixes with "'b"

void MtCursor::emit_number_literal(TSNode n) {
  std::string body = mod->body(n);
  if (body.starts_with("0x")) {
    emit_replacement(n, "'h%s", body.c_str() + 2);
  }
  else if (body.starts_with("0b")) {
    emit_replacement(n, "'b%s", body.c_str() + 2);
  }
  else {
    emit(n);
  }
}

//------------------------------------------------------------------------------
// Change "return x" to "(funcname) = x" to match old Verilog return style.

void MtCursor::emit_return_statement(TSNode n) {
  auto func_name = mod->body(current_function_name);
  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_return:
      emit_replacement(c, "%s =", func_name.c_str());
      break;
    default:
      emit_dispatch(c);
      break;
    }
  }
}

//------------------------------------------------------------------------------
// FIXME translate types here

void MtCursor::emit_primitive_type(TSNode n) {
  emit(n);
}

//------------------------------------------------------------------------------
// FIXME translate types here

void MtCursor::emit_type_identifier(TSNode n) {
  emit(n);
}

//------------------------------------------------------------------------------
// For some reason the class's trailing semicolon ends up with the template decl, so we prune it here.

void MtCursor::emit_template_declaration(TSNode n) {
  for (auto c : n) {
    switch (c.sym) {
    case anon_sym_template: {
      skip_over(c);
      skip_whitespace();
      break;
    }
    case anon_sym_SEMI:
      skip_over(c);
      break;
    default:
      emit_dispatch(c);
      break;
    }
  }
}

//------------------------------------------------------------------------------
// Replace foo.bar.baz with foo_bar_baz, so that a field expression instead
// refers to a glue expression.

void MtCursor::emit_flat_field_expression(TSNode n) {
  auto field = mod->body(n);
  for (auto& c : field) if (c == '.') c = '_';
  emit_replacement(n, field.c_str());
}

//------------------------------------------------------------------------------

void MtCursor::emit_dispatch(TSNode n) {
  assert(cursor <= mod->start(n));

  auto s = ts_node_symbol(n);

  switch (s) {
  case anon_sym_template:
  case anon_sym_if:
  case anon_sym_else:
  case anon_sym_typedef:
  case anon_sym_enum:
  case anon_sym_default:
  case anon_sym_LF:
  case anon_sym_EQ:
  case anon_sym_SEMI:
  case anon_sym_COMMA:
  case anon_sym_LPAREN:
  case anon_sym_RPAREN:
  case anon_sym_LBRACK:
  case anon_sym_RBRACK:
  case anon_sym_BANG:
  case anon_sym_EQ_EQ:
  case anon_sym_AMP:
  case anon_sym_AMP_AMP:
  case anon_sym_DASH:
  case anon_sym_PLUS:
  case anon_sym_PIPE:
  case anon_sym_PIPE_PIPE:
  case anon_sym_LT:
  case anon_sym_LT_LT:
  case anon_sym_LT_EQ:
  case anon_sym_GT_GT:
  case anon_sym_BANG_EQ:
  case anon_sym_COLON:
  case aux_sym_preproc_include_token1:
    emit(n);
    break;

  case alias_sym_field_identifier:
  case sym_identifier:
  case sym_true:
  case sym_false:
  case sym_comment:
  case sym_string_literal:
    emit(n);
    break;

  case sym_parameter_list:
    for (auto c : n) emit_dispatch(c);
    skip_whitespace();
    break;

  case sym_if_statement:
  case sym_for_statement:
  case sym_parenthesized_expression:
  case sym_parameter_declaration:
  case sym_optional_parameter_declaration:
  case sym_condition_clause:
  case sym_unary_expression:
  case sym_subscript_expression:
  case sym_enum_specifier:
  case sym_enumerator:
  case sym_type_definition:
  case sym_expression_statement:
  case sym_declaration:
  case sym_binary_expression:
  case sym_argument_list:
  case sym_array_declarator:
  case sym_type_descriptor:
  case sym_function_declarator:
  case sym_init_declarator:
    for (auto c : n) emit_dispatch(c);
    break;

  case sym_number_literal:         emit_number_literal(n); break;
  case sym_field_expression:       emit_flat_field_expression(n); break;
  case sym_return_statement:       emit_return_statement(n); break;
  case sym_template_declaration:   emit_template_declaration(n); break;
  case sym_preproc_include:        emit_preproc_include(n);      break;
  case sym_field_declaration:      emit_field_declaration(n);  break;
  case sym_compound_statement:     emit_compound_statement(n); break;
  case sym_template_type:          emit_template_type(n);      break;
  case sym_translation_unit:       emit_translation_unit(n); break;
  case sym_primitive_type:         emit_primitive_type(n); break;
  case alias_sym_type_identifier:  emit_type_identifier(n); break;
  case sym_class_specifier:        emit_class_specifier(n); break;
  case sym_struct_specifier:       emit_class_specifier(n); break;
  case sym_function_definition:    emit_function_definition(n); break;
  case sym_call_expression:        emit_call_expression(n); break;
  case sym_assignment_expression:  emit_assignment_expression(n); break;
  case sym_template_argument_list: emit_template_argument_list(n); break;
  case sym_enumerator_list:        emit_enumerator_list(n); break;

  case sym_case_statement: {
    for (auto c : n) {
      if (c.sym == anon_sym_case) {
        skip_over(c);
        skip_whitespace();
      }
      else emit_dispatch(c);
    }
    break;
  }

  case sym_switch_statement: {
    for (auto c : n) {
      if (c.sym == anon_sym_switch) {
        emit_replacement(c, "case");
      }
      else if (c.field == field_body) {
        for (auto gc : c) {
          if (gc.sym == anon_sym_LBRACE) skip_over(gc);
          else if (gc.sym == anon_sym_RBRACE) emit_replacement(gc, "endcase");
          else emit_dispatch(gc);
        }

      }
      else {
        emit_dispatch(c);
      }
    }
    break;
  }

  case sym_storage_class_specifier: {
    if (mod->match(n, "static")) {
      emit_replacement(n, "localparam");
    }
    else {
      comment_out(n);
    }
    break;
  }

  case sym_access_specifier:
  case sym_type_qualifier:
    skip_over(n);
    skip_whitespace();
    break;

  case sym_preproc_call:
  case sym_preproc_if:
    skip_over(n);
    skip_whitespace();
    break;

  case sym_template_parameter_list:
    mod->module_param_list = n;
    skip_over(n);
    skip_whitespace();
    break;

  default:
    printf("\n\n\n########################################\n");
    mod->dump_tree(n);
    printf("\n########################################\n\n\n");
    debugbreak();
  }
}

//------------------------------------------------------------------------------
