#include "MtCursor.h"

#include <memory.h>

#include "Log.h"
#include "MtField.h"
#include "MtFuncParam.h"
#include "MtMethod.h"
#include "MtModLibrary.h"
#include "MtModParam.h"
#include "MtModule.h"
#include "MtNode.h"
#include "MtSourceFile.h"
#include "Platform.h"

//------------------------------------------------------------------------------

MtCursor::MtCursor(MtModLibrary* lib, MtSourceFile* source, MtModule* mod,
                   std::string* out)
    : lib(lib), current_source(source), current_mod(mod), str_out(out) {
  indent_stack.push_back("");
  cursor = current_source->source;

  // FIXME preproc_vars should be a set
  preproc_vars["IV_TEST"] = MnNode();
}

//------------------------------------------------------------------------------

void MtCursor::push_indent(MnNode body) {
  assert(body.sym == sym_compound_statement ||
         body.sym == sym_field_declaration_list);

  auto n = body.first_named_child().node;
  if (ts_node_is_null(n)) {
    indent_stack.push_back("");
    return;
  }

  if (ts_node_symbol(n) == sym_access_specifier) {
    n = ts_node_next_sibling(n);
  }
  const char* begin = &current_source->source[ts_node_start_byte(n)] - 1;
  const char* end = &current_source->source[ts_node_start_byte(n)];

  std::string indent;

  while (*begin != '\n' && *begin != '{') begin--;
  if (*begin == '{') {
    indent = "";
  } else {
    indent = std::string(begin + 1, end);
  }

  for (auto& c : indent) {
    assert(isspace(c));
  }

  indent_stack.push_back(indent);
}

void MtCursor::pop_indent(MnNode class_body) { indent_stack.pop_back(); }

//------------------------------------------------------------------------------
// Generic emit() methods

CHECK_RETURN Err MtCursor::emit_newline() { return emit_char('\n'); }

CHECK_RETURN Err MtCursor::emit_backspace() {
  Err err;
  if (echo) {
    LOG_C(0x8080FF, "^H");
  }
  str_out->pop_back();
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_indent() {
  Err err;
  for (auto c : indent_stack.back()) {
    err << emit_char(c);
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_char(char c, uint32_t color) {
  Err err;
  // We ditch all '\r'.
  if (c == '\r') return err;

  if (c < 0 || !isspace(c)) {
    line_dirty = true;
  }

  if (c == '\n') {
    // Strip trailing whitespace
    while (str_out->size() && str_out->back() == ' ') {
      err << emit_backspace();
    }

    // Discard the line if it contained only whitespace after elisions
    if (!line_dirty && line_elided) {
      if (echo) {
        LOG_C(0xFF8080, " (Line elided)");
      }
      while (str_out->size() && str_out->back() != '\n') {
        str_out->pop_back();
      }
    } else {
      str_out->push_back(c);
    }

    line_dirty = false;
    line_elided = false;
  } else {
    str_out->push_back(c);
  }

  if (echo) {
    LOG_C(color, "%c", c);
  }

  at_newline = c == '\n';
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_ws() {
  Err err;
  while (cursor < current_source->source_end && isspace(*cursor)) {
    err << emit_char(*cursor++);
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_ws_to(const MnNode& n) {
  Err err;
  while (cursor < current_source->source_end && isspace(*cursor) &&
         cursor < n.start()) {
    err << emit_char(*cursor++);
  }

  if (cursor != n.start()) {
    err << ERR("emit_ws_to - did not hit node %s\n", n.text().c_str());
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_ws_to(TSSymbol sym, const MnNode& n) {
  Err err = emit_ws();
  if (n.sym != sym) {
    err << ERR("emit_ws_to - bad sym, %d != node %s\n", sym, n.ts_node_type());
  }

  if (cursor != n.start()) {
    err << ERR("emit_ws_to - did not hit node %s\n", n.text().c_str());
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_ws_to_newline() {
  Err err;
  if (at_newline) return err;

  while (cursor < current_source->source_end && isspace(*cursor)) {
    auto c = *cursor++;
    err << emit_char(c);
    if (c == '\n') {
      return err;
    }
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::skip_over(MnNode n) {
  Err err = emit_ws_to(n);
  if (n.is_null()) {
    err << ERR("Skipping over null node\n");
  } else {
    if (echo) {
      LOG_C(0x8080FF, "%s", n.text().c_str());
    }

    if (cursor != n.start()) {
      err << ERR("Skipping over node, but we're not at its start point\n");
    }
    cursor = n.end();
    line_elided = true;
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::skip_ws() {
  Err err;

  while (*cursor && isspace(*cursor) && (*cursor != '\n')) {
    if (echo) {
      LOG_C(0x8080FF, "_");
    }
    cursor++;
  }

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::prune_trailing_ws() {
  Err err;

  while (isspace(str_out->back())) {
    err << emit_backspace();
  }

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::comment_out(MnNode n) {
  Err err = emit_ws_to(n);

  if (cursor != n.start()) err << ERR("comment_out did not start with cursor at node start\n");

  err << emit_print("/*");

  auto start = n.start();
  auto end1 = n.end();
  auto end2 = end1;

  // If the block ends in a newline, put the comment terminator _before_ the newline
  if (end1[-1] == '\n') end1--;

  err << emit_span(start, end1);
  err << emit_print("*/");
  if (end1 != end2)err << emit_span(end1, end2);

  cursor = n.end();

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_span(const char* a, const char* b) {
  Err err;

  if (a == b) return err << ERR("Empty span\n");

  for (auto c = a; c < b; c++) {
    err << emit_char(*c);
  }
  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_text(MnNode n) {
  Err err = emit_ws_to(n);

  if (cursor != n.start()) {
    return err << ERR("MtCursor::emit_text - cursor not at start of node\n");
  }

  err << emit_span(n.start(), n.end());
  cursor = n.end();

  return err << check_done(n);
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_print(const char* fmt, ...) {
  Err err;

  va_list args;
  va_start(args, fmt);
  int size = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  auto buf = new char[size + 1];

  va_start(args, fmt);
  vsnprintf(buf, size_t(size) + 1, fmt, args);
  va_end(args);

  for (int i = 0; i < size; i++) {
    err << emit_char(buf[i], 0x80FF80);
  }
  delete[] buf;

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_replacement(MnNode n, const char* fmt, ...) {
  Err err = emit_ws_to(n);

  cursor = n.end();

  va_list args;
  va_start(args, fmt);
  int size = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  auto buf = new char[size + 1];

  va_start(args, fmt);
  vsnprintf(buf, size_t(size) + 1, fmt, args);
  va_end(args);

  for (int i = 0; i < size; i++) {
    err << emit_char(buf[i], 0x80FFFF);
  }
  delete[] buf;

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_initializer_list(MnNode node) {
  Err err = emit_ws_to(sym_initializer_list, node);

  bool has_zero = false;
  bool bad_list = false;
  for (auto child : node) {
    if (!child.is_named()) continue;
    if (child.sym == sym_number_literal) {
      if (child.text() == "0") {
        if (has_zero) bad_list = true;
        has_zero = true;
      }
      else {
        bad_list = true;
      }
    }
    else {
      bad_list = true;
    }
  }

  if (has_zero && !bad_list) {
    err << emit_replacement(node, "'0");
  }
  else {
    for (auto child : node) {
      switch (child.sym) {
        case sym_identifier:
          err << emit_identifier(child);
          break;
        case sym_assignment_expression:
          err << emit_expression(child);
          break;
        default:
          err << emit_default(child);
          break;
      }
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Replace "#include" with "`include" and ".h" with ".sv"

CHECK_RETURN Err MtCursor::emit_sym_preproc_include(MnNode n) {
  Err err = emit_ws_to(sym_preproc_include, n);

  auto path = n.get_field(field_path).text();
  if (!path.ends_with(".h\"")) return err << ERR("Include did not end with .h\n");

  path.resize(path.size() - 3);
  path = path + ".sv\"";

  for (auto child : n) {
    if (child.sym == aux_sym_preproc_include_token1) {
      err << emit_replacement(child, "`include");
    } else if (child.field == field_path) {
      err << emit_replacement(child, path.c_str());
    } else {
      err << emit_default(child);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Change '=' to '<=' if lhs is a field and we're inside a sequential block.

CHECK_RETURN Err MtCursor::emit_sym_assignment_expression(MnNode node) {
  Err err = emit_ws_to(sym_assignment_expression, node);

  auto lhs = node.get_field(field_left);
  auto op  = node.get_field(field_operator).text();
  auto rhs = node.get_field(field_right);
  bool left_is_field = current_mod->get_field(lhs.name4()) != nullptr;

  bool is_compound = op != "=";


  for (auto child : node) {
    switch (child.field) {
      case field_left: err << emit_expression(child); break;
      case field_operator:
        // There may not be a method if we're in an enum initializer list.
        if (current_method && current_method->is_tick_ && left_is_field) {
          err << emit_replacement(child, "<=");
        } else {
          err << emit_replacement(child, "=");
        }
        if (is_compound) {
          push_cursor(lhs);
          err << emit_print(" ");
          err << emit_expression(lhs);
          err << emit_print(" %c", op[0]);
          pop_cursor(lhs);
        }

        break;
      case field_right: err << emit_expression(child); break;
      default: err << emit_default(child); break;
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_static_bit_extract(MnNode call, int bx_width) {
  Err err = emit_ws_to(call);

  int arg_count = call.get_field(field_arguments).named_child_count();

  auto arg0 = call.get_field(field_arguments).named_child(0);
  auto arg1 = call.get_field(field_arguments).named_child(1);

  auto arg0_text = arg0.text();

  /*
  if (current_method->has_param(arg0_text)) {
    //printf("bit extract!\n");
    arg0_text = std::string(current_method->cname()) + "_" + arg0_text;
  }
  */

  /*
  auto arg0_field = current_mod->get_field(arg0_text);
  if (arg0_field) {
    if (arg0_field->is_input()) {
      printf("INPUT FIELD %s!\n", arg0_field->cname());
    }
  }
  */


  if (arg_count == 1) {
    if (arg0.sym == sym_number_literal) {
      // Explicitly sized literal - 8'd10

      cursor = arg0.start();
      err << emit_sym_number_literal(MnNode(arg0), bx_width);
      cursor = call.end();
    } else if (arg0.sym == sym_identifier ||
               arg0.sym == sym_subscript_expression) {
      if (arg0.text() == "DONTCARE") {
        // Size-casting expression
        err << emit_print("%d'bx", bx_width);
        cursor = call.end();
      } else {
        // Size-casting expression
        cursor = arg0.start();
        err << emit_print("%d'(", bx_width) << emit_expression(arg0)
            << emit_print(")");
        cursor = call.end();
      }

    } else {
      // Size-casting expression
      cursor = arg0.start();
      err << emit_print("%d'(", bx_width) << emit_expression(arg0)
          << emit_print(")");
      cursor = call.end();
    }
  } else if (arg_count == 2) {
    // Slicing logic array - foo[7:2]

    if (bx_width == 1) {
      // b1(foo, 7) -> foo[7]
      cursor = arg0.start();
      err << emit_expression(arg0);
      err << emit_print("[");
      cursor = arg1.start();
      err << emit_expression(arg1);
      err << emit_print("]");
      cursor = call.end();
    }
    else {
      cursor = arg0.start();
      err << emit_expression(arg0);
      err << emit_print("[");

      if (arg1.sym == sym_number_literal) {
        // Static size slice, static offset
        // b6(foo, 2) -> foo[7:2]
        int offset = atoi(arg1.start());
        err << emit_print("%d:%d", bx_width - 1 + offset, offset);
      } else {
        // Static size slice, dynamic offset
        // b6(foo, offset) -> foo[5 + offset:offset]
        err << emit_print("%d + ", bx_width - 1);
        cursor = arg1.start();
        err << emit_expression(arg1);
        err << emit_print(" : ");
        cursor = arg1.start();
        err << emit_expression(arg1);
      }
      err << emit_print("]");
      cursor = call.end();

    }
  } else {
    err << ERR("emit_static_bit_extract got > 1 args\n");
  }

  return err << check_done(call);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_dynamic_bit_extract(MnNode call,
                                                    MnNode bx_node) {
  Err err = emit_ws_to(call);

  int arg_count = call.get_field(field_arguments).named_child_count();

  auto arg0 = call.get_field(field_arguments).named_child(0);
  auto arg1 = call.get_field(field_arguments).named_child(1);

  auto arg0_text = arg0.text();

  auto arg0_field = current_mod->get_field(arg0_text);
  if (arg0_field) {
    if (arg0_field->is_input()) {
      printf("INPUT FIELD %s!\n", arg0_field->cname());
    }
  }

  if (arg_count == 1) {
    // Non-literal size-casting expression - bits'(expression)
    if (arg0.text() == "DONTCARE") {
      cursor = bx_node.start();
      err << emit_expression(bx_node);
      err << emit_print("'('x)");
      cursor = call.end();
    } else {
      cursor = bx_node.start();
      err << emit_print("(");
      err << emit_expression(bx_node);
      err << emit_print(")");
      err << emit_print("'(");
      cursor = arg0.start();
      err << emit_expression(arg0);
      err << emit_print(")");
      cursor = call.end();
    }

  } else if (arg_count == 2) {
    // Non-literal slice expression - expression[bits+offset-1:offset];

    cursor = arg0.start();
    err << emit_expression(arg0);

    if (arg1.sym != sym_number_literal) {
      err << ERR("emit_dynamic_bit_extract saw a non-literal?\n");
    }
    int offset = atoi(arg1.start());

    err << emit_print("[%s+%d:%d]", bx_node.text().c_str(), offset - 1, offset);
    cursor = call.end();
  } else {
    err << ERR("emit_dynamic_bit_extract saw too many args?\n");
  }

  return err << check_done(call);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_simple_call(MnNode n) {
  Err err;

  for (auto c : n) {
    if (c.field == field_function)
      err << emit_identifier(c);
    else if (c.field == field_arguments)
      err << emit_sym_argument_list(c);
    else
      err << emit_default(c);
  }

  return err;
}


//------------------------------------------------------------------------------

CHECK_RETURN bool MtCursor::can_omit_call(MnNode n) {
  MnNode func = n.get_field(field_function);
  MnNode args = n.get_field(field_arguments);

  std::string func_name = func.name4();

  auto method = current_mod->get_method(func_name);

  if (func.sym == sym_field_expression) {
    auto node_component = func.get_field(field_argument);
    auto node_method = func.get_field(field_field);

    auto dst_component = current_mod->get_field(node_component.text());
    auto dst_method = dst_component->_type_mod->get_method(node_method.text());

    if (dst_method && !dst_method->has_return()) {
      return true;
    }
  }
  else {
    auto dst_method = current_mod->get_method(func.name4());
    if (dst_method && dst_method->needs_binding) {
      return true;
    }
  }

  return false;
}

//------------------------------------------------------------------------------
// Replace function names with macro names where needed, comment out explicit
// init/tick/tock calls.

CHECK_RETURN Err MtCursor::emit_sym_call_expression(MnNode n) {
  Err err = emit_ws_to(sym_call_expression, n);

  MnNode func = n.get_field(field_function);
  MnNode args = n.get_field(field_arguments);

  std::string func_name = func.name4();

  auto method = current_mod->get_method(func_name);

  //----------
  // Handle bN

  if (func_name[0] == 'b' && func_name.size() == 2) {
    auto x = func_name[1];
    if (x >= '0' && x <= '9') {
      return emit_static_bit_extract(n, x - '0');
    }
  }

  if (func_name[0] == 'b' && func_name.size() == 3) {
    auto x = func_name[1];
    auto y = func_name[2];
    if (x >= '0' && x <= '9' && y >= '0' && y <= '9') {
      return emit_static_bit_extract(n, (x - '0') * 10 + (y - '0'));
    }
  }

  //----------

  assert(cursor == n.start());

  if (func_name == "coerce") {
    // Convert to cast? We probably shouldn't be calling coerce() directly.
    err << ERR("Shouldn't be calling coerce() directly?\n");

  } else if (func_name == "sra") {
    auto lhs = args.named_child(0);
    auto rhs = args.named_child(1);

    err << emit_print("($signed(");
    cursor = lhs.start();
    err << emit_expression(lhs);
    err << emit_print(") >>> ");
    cursor = rhs.start();
    err << emit_expression(rhs);
    err << emit_print(")");
    cursor = n.end();

  } else if (func_name == "signed") {
    err << emit_replacement(func, "$signed");
    err << emit_sym_argument_list(args);
  } else if (func_name == "unsigned") {
    err << emit_replacement(func, "$unsigned");
    err << emit_sym_argument_list(args);
  } else if (func_name == "clog2") {
    err << emit_replacement(func, "$clog2");
    err << emit_sym_argument_list(args);
  } else if (func_name == "pow2") {
    err << emit_replacement(func, "2**");
    err << emit_sym_argument_list(args);
  } else if (func_name == "readmemh") {
    err << emit_replacement(func, "$readmemh");
    err << emit_sym_argument_list(args);
  } else if (func_name == "value_plusargs") {
    err << emit_replacement(func, "$value$plusargs");
    err << emit_sym_argument_list(args);
  } else if (func_name == "write") {
    err << emit_replacement(func, "$write");
    err << emit_sym_argument_list(args);
  } else if (func_name == "sign_extend") {
    err << emit_replacement(func, "$signed");
    err << emit_sym_argument_list(args);
  } else if (func_name == "zero_extend") {
    err << emit_replacement(func, "$unsigned");
    err << emit_sym_argument_list(args);
  }
  else if (func_name == "bx") {
    // Bit extract.
    auto template_arg = func.get_field(field_arguments).named_child(0);
    err << emit_dynamic_bit_extract(n, template_arg);
  }
  else if (func_name == "cat") {
    // Remove "cat" and replace parens with brackets
    err << skip_over(func);
    for (const auto& arg : (MnNode&)args) {
      switch (arg.sym) {
        case anon_sym_LPAREN:
          err << emit_replacement(arg, "{");
          break;
        case anon_sym_RPAREN:
          err << emit_replacement(arg, "}");
          break;
        default:
          err << emit_expression(arg);
          break;
      }
    }
  }
  else if (func_name == "dup") {
    // Convert "dup<15>(x)" to "{15 {x}}"

    if (args.named_child_count() != 1) return err << ERR("dup() had too many children\n");

    err << skip_over(func);

    auto template_arg = func.get_field(field_arguments).named_child(0);
    int dup_count = atoi(template_arg.start());
    err << emit_print("{%d ", dup_count);
    err << emit_print("{");

    auto func_arg = args.named_child(0);
    cursor = func_arg.start();
    err << emit_expression(func_arg);

    err << emit_print("}}");
    cursor = n.end();
  }

  else if (func.sym == sym_field_expression) {
    // Component method call.

    auto node_component = n.get_field(field_function).get_field(field_argument);

    auto dst_component = current_mod->get_field(node_component.text());
    auto dst_mod = lib->get_module(dst_component->type_name());
    if (!dst_mod) return err << ERR("dst_mod null\n");
    auto node_func = n.get_field(field_function).get_field(field_field);
    auto dst_method = dst_mod->get_method(node_func.text());

    if (!dst_method) return err << ERR("dst_method null\n");

    if (dst_method->has_return()) {
      err << emit_print("%s_%s_ret", node_component.text().c_str(), dst_method->cname());
    }
    else {
      err << comment_out(func);
    }
    cursor = n.end();
  }
  else {
    // Internal method call.

    if (method->needs_binding) {
      err << emit_replacement(n, "%s_ret", method->cname());
    }
    else {
      err << emit_simple_call(n);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Replace "logic blah = x;" with "logic blah;"

CHECK_RETURN Err MtCursor::emit_init_declarator_as_decl(MnNode n) {
  Err err = emit_ws_to(n);

  // FIXME loop style

  err << emit_type(n.get_field(field_type));
  err << emit_identifier(n.get_field(field_declarator).get_field(field_declarator));
  err << prune_trailing_ws();
  err << emit_print(";");
  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Replace "logic blah = x;" with "blah = x;"

CHECK_RETURN Err MtCursor::emit_init_declarator_as_assign(MnNode n) {
  Err err = emit_ws_to(n);

  // FIXME loop style

  auto decl = n.get_field(field_declarator);

  // We don't need to emit anything for decls without initialization values.
  if (decl.sym != sym_init_declarator) {
    err << skip_over(n);
  } else {
    cursor = decl.start();
    err << emit_declarator(decl);
    err << prune_trailing_ws();
    err << emit_print(";");
    cursor = n.end();
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Emit local variable declarations at the top of the block scope.

CHECK_RETURN Err MtCursor::emit_hoisted_decls(MnNode n) {
  Err err;
  bool any_to_hoist = false;

  for (const auto& c : (MnNode&)n) {
    if (c.sym == sym_declaration) {
      bool is_localparam = c.sym == sym_declaration && c.is_const();

      if (is_localparam) {
      } else {
        any_to_hoist = true;
        break;
      }
    }

    if (c.sym == sym_for_statement) {
      auto init = c.get_field(field_initializer);
      if (init.sym == sym_declaration) {
        any_to_hoist = true;
      }
    }

  }

  if (!any_to_hoist) {
    return err;
  }

  MtCursor old_cursor = *this;
  for (const auto& c : (MnNode&)n) {
    if (c.sym == sym_declaration) {
      bool is_localparam = c.sym == sym_declaration && c.is_const();

      if (is_localparam) {
        // fixme?
        err << ERR("FIXME - emit_hoisted_decls emitting a localparam?\n");
      } else {
        cursor = c.start();
        err << emit_indent();
        err << emit_sym_declaration(c, false, true);
        err << emit_newline();
      }
    }

    if (c.sym == sym_for_statement) {
      auto init = c.get_field(field_initializer);
      if (init.sym == sym_declaration) {
        cursor = init.start();
        err << emit_indent();
        err << emit_sym_declaration(init, false, true);
        err << emit_newline();
      }
    }
  }
  *this = old_cursor;

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_local_call_arg_binding(MtMethod* method, MnNode param, MnNode val) {
  Err err;

  auto param_name = param.get_field(field_declarator).text();

  err << emit_indent();
  err << emit_print("%s_%s = ", method->cname(), param_name.c_str());

  cursor = val.start();
  err << emit_expression(val);
  cursor = val.end();

  err << prune_trailing_ws();
  err << emit_print(";");
  err << emit_newline();

  return err;
}

CHECK_RETURN Err MtCursor::emit_component_call_arg_binding(MnNode inst, MtMethod* method, MnNode param, MnNode val) {
  Err err;

  err << emit_indent();
  err << emit_print("%s_%s_%s = ", inst.text().c_str(),
                    method->name().c_str(),
                    param.get_field(field_declarator).text().c_str());

  cursor = val.start();
  err << emit_expression(val);
  cursor = val.end();

  err << prune_trailing_ws();
  err << emit_print(";");
  err << emit_newline();

  return err;
}
//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_call_arg_bindings(MnNode n) {
  Err err;
  auto old_cursor = cursor;

  // Emit bindings for child nodes first, but do _not_ recurse into compound
  // blocks.

  for (auto c : n) {
    if (c.sym != sym_compound_statement) {
      err << emit_call_arg_bindings(c);
    }
  }

  // OK, now we can emit bindings for the call we're at.

  if (n.sym == sym_call_expression) {
    auto func_node = n.get_field(field_function);
    auto args_node = n.get_field(field_arguments);

    if (func_node.sym == sym_field_expression) {
      if (args_node.named_child_count() != 0) {
        auto inst_id = func_node.get_field(field_argument);
        auto meth_id = func_node.get_field(field_field);

        auto component = current_mod->get_component(inst_id.text());
        assert(component);

        auto component_method = component->_type_mod->get_method(meth_id.text());
        if (!component_method) return err << ERR("Component method missing\n");

        for (int i = 0; i < component_method->param_nodes.size(); i++) {
          err << emit_component_call_arg_binding(
            inst_id,
            component_method,
            component_method->param_nodes[i],
            args_node.named_child(i)
          );
        }
      }
    }
    else if (func_node.sym == sym_identifier) {
      auto method = current_mod->get_method(func_node.text().c_str());
      if (method && method->needs_binding) {
        for (int i = 0; i < method->param_nodes.size(); i++) {
          err << emit_local_call_arg_binding(
            method,
            method->param_nodes[i],
            args_node.named_child(i)
          );
        }
      }
    }
  }

  cursor = old_cursor;
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_as_init(MnNode n) {
  Err err;

  auto func_decl = n.get_field(field_declarator);
  auto func_init = n.child_by_sym(sym_field_initializer_list);
  auto func_body = n.get_field(field_body);

  auto func_params = func_decl.get_field(field_parameters);

  err << emit_ws_to(func_decl);
  err << emit_module_parameter_list(func_params);

  err << emit_replacement(func_decl, "initial");
  if (func_init) {
    err << skip_over(func_init);
    err << skip_ws();
  }
  err << emit_sym_compound_statement(func_body, "begin", "end");
  assert(cursor == n.end());

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_as_func(MnNode n) {
  Err err;

  auto func_ret = n.get_field(field_type);
  auto func_decl = n.get_field(field_declarator);
  auto func_body = n.get_field(field_body);

  err << emit_print("function ");
  err << emit_type(func_ret);
  err << emit_declarator(func_decl);
  err << prune_trailing_ws();
  err << emit_print(";");
  err << emit_sym_compound_statement(func_body, "", "endfunction");

  assert(cursor == n.end());
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_as_task(MnNode n) {
  Err err;

  auto func_ret = n.get_field(field_type);
  auto func_decl = n.get_field(field_declarator);
  auto func_body = n.get_field(field_body);

  err << emit_print("task automatic ");
  err << skip_over(func_ret);
  err << skip_ws();
  err << emit_declarator(func_decl);
  err << prune_trailing_ws();
  err << emit_print(";");
  err << emit_sym_compound_statement(func_body, "", "endtask");

  assert(cursor == n.end());
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_as_always_comb(MnNode n) {
  Err err;

  auto func_ret = n.get_field(field_type);
  auto func_decl = n.get_field(field_declarator);
  auto func_body = n.get_field(field_body);
  auto func_params = func_decl.get_field(field_parameters);

  auto old_replacements = id_replacements;
  for (auto c : func_params) {
    if (!c.is_named()) continue;
    id_replacements[c.name4()] = func_decl.name4() + "_" + c.name4();
  }

  err << emit_print("always_comb begin : %s", func_decl.name4().c_str());
  err << skip_over(func_ret);
  err << skip_ws();
  err << skip_over(func_decl);
  err << skip_ws();
  err << emit_sym_compound_statement(func_body, "", "end");

  id_replacements = old_replacements;

  assert(cursor == n.end());
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_as_always_ff(MnNode n) {
  Err err;

  auto func_decl = n.get_field(field_declarator);
  auto func_params = func_decl.get_field(field_parameters);

  auto old_replacements = id_replacements;
  for (auto c : func_params) {
    if (c.sym == sym_parameter_declaration) {
      id_replacements[c.name4()] = func_decl.name4() + "_" + c.name4();
    }
  }

  for (auto c : n) {
    switch(c.field) {
    case field_type:       err << emit_replacement(c, "always_ff @(posedge clock) begin : %s", func_decl.name4().c_str()); break;
    case field_declarator: err << skip_over(c); break;
    case field_body:       err << emit_sym_compound_statement(c, "", "end"); break;
    default:               err << emit_default(c); break;
    }
  }

  id_replacements = old_replacements;

  assert(cursor == n.end());
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_trigger_comb(MnNode n) {
  Err err;
  err << emit_indent();
  err << emit_print("always_comb ");
  err << emit_trigger_call(current_method);
  err << emit_newline();
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_func_trigger_ff(MnNode n) {
  Err err;
  err << emit_indent();
  err << emit_print("always_ff @(posedge clock) ");
  err << emit_trigger_call(current_method);
  err << emit_newline();
  return err;
}

//------------------------------------------------------------------------------
// Change "init/tick/tock" to "initial begin / always_comb / always_ff", change
// void methods to tasks, and change const methods to funcs.

// func_def = { field_type, field_declarator, field_body }

// Tock func with no return is the problematic one for Yosys.
// We can't emit as task, we're in tock and that would break sensitivity

CHECK_RETURN Err MtCursor::emit_sym_function_definition(MnNode n) {
  Err err = emit_ws_to(sym_function_definition, n);

  auto return_type = n.get_field(field_type);
  auto func_decl = n.get_field(field_declarator);

  current_method = current_mod->get_method(n.name4());
  assert(current_method);

  //----------
  // Emit a block declaration for the type of function we're in.

  if (current_method->emit_as_always_comb) {
    err << emit_func_as_always_comb(n);
  }

  else if (current_method->emit_as_always_ff) {
    err << emit_func_as_always_ff(n);
  }

  else if (current_method->emit_as_init) {
    err << emit_func_as_init(n);
  }

  else if (current_method->emit_as_task) {
    err << emit_func_as_task(n);
  }

  else if (current_method->emit_as_func) {
    err << emit_func_as_func(n);
  }

  err << emit_ws_to_newline();

  if (current_method->needs_binding) {
    err << emit_func_binding_vars(current_method);
  }

  //----------

  current_method = nullptr;
  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_component_port_list(MnNode n) {
  Err err;

  auto node_type = n.child(0);  // type
  auto node_decl = n.child(1);  // decl
  auto node_semi = n.child(2);  // semi

  auto inst_name = node_decl.text();
  auto component_mod = lib->get_module(n.type5());

  cursor = node_type.start();
  err << emit_type(node_type);

  bool has_template_params = node_type.sym == sym_template_type && component_mod->mod_param_list;
  bool has_constructor_params = component_mod->constructor && component_mod->constructor->param_nodes.size();

  if (has_template_params || has_constructor_params) {

    err << emit_print(" #(");
    err << emit_newline();
    indent_stack.push_back(indent_stack.back() + "  ");

    // Count up how many parameters we're passing to the submodule.
    int param_count = 0;

    // All the named elements of the template argument list count as parameters.
    if (has_template_params) {
      auto template_args = node_type.get_field(field_arguments);
      for (auto c : template_args) {
        if (c.is_named()) param_count++;
      }
    }

    // If the field initializer for this component passes arguments to the component's constructor,
    // count them as parameters.
    if (has_constructor_params && current_mod->constructor) {
      for(auto initializer : current_mod->constructor->_node.child_by_sym(sym_field_initializer_list)) {
        if (initializer.sym != sym_field_initializer) continue;
        if (initializer.child_by_sym(alias_sym_field_identifier).text() == inst_name) {
          for (auto c : initializer.child_by_sym(sym_argument_list)) {
            if (c.is_named()) {
              param_count++;
            }
          }
          break;
        }
      }
    }

    // Emit template arguments as module parameters
    if (has_template_params) {
      err << emit_indent();
      err << emit_print("// Template Parameters");
      err << emit_newline();

      auto template_args = node_type.get_field(field_arguments);

      std::vector<MnNode> params;
      std::vector<MnNode> args;

      for (auto c : component_mod->mod_param_list) {
        if (c.is_named()) params.push_back(c);
      }

      for (auto c : template_args) {
        if (c.is_named()) args.push_back(c);
      }

      for (int param_index = 0; param_index < args.size(); param_index++) {
        MtCursor sub_cursor = *this;
        sub_cursor.cursor = args[param_index].start();

        auto param = params[param_index];
        auto arg = args[param_index];

        err << sub_cursor.emit_indent();
        err << sub_cursor.emit_print(".%s(", param.name4().c_str());
        err << sub_cursor.emit_expression(arg);
        err << sub_cursor.emit_print(")");
        if(--param_count) err << sub_cursor.emit_print(",");
        err << sub_cursor.emit_newline();
      }
    }

    // Emit constructor arguments as module parameters
    if (has_constructor_params) {
      err << emit_indent();
      err << emit_print("// Constructor Parameters");
      err << emit_newline();

      // The parameter names come from the submodule's constructor
      const auto& params = component_mod->constructor->param_nodes;

      // Find the initializer node for the component and extract arguments
      std::vector<MnNode> args;
      if (current_mod->constructor) {
        for(auto initializer : current_mod->constructor->_node.child_by_sym(sym_field_initializer_list)) {
          if (initializer.sym != sym_field_initializer) continue;
          if (initializer.child_by_sym(alias_sym_field_identifier).text() == inst_name) {
            for (auto c : initializer.child_by_sym(sym_argument_list)) {
              if (c.is_named()) args.push_back(c);
            }
            break;
          }
        }
      }

      for (int param_index = 0; param_index < args.size(); param_index++) {
        MtCursor sub_cursor = *this;
        sub_cursor.cursor = args[param_index].start();

        auto param = params[param_index];
        auto arg = args[param_index];

        err << sub_cursor.emit_indent();
        err << sub_cursor.emit_print(".%s(", param.name4().c_str());
        err << sub_cursor.emit_expression(arg);
        err << sub_cursor.emit_print(")");
        if(--param_count) err << sub_cursor.emit_print(",");
        err << sub_cursor.emit_newline();
      }
    }
    indent_stack.pop_back();

    err << emit_indent();
    err << emit_print(")");
  }

  //err << emit_indent();
  err << emit_identifier(node_decl);
  err << emit_print("(");
  err << emit_newline();
  indent_stack.push_back(indent_stack.back() + "  ");


  if (component_mod->needs_tick()) {
    err << emit_indent();
    err << emit_print("// Global clock");
    err << emit_newline();
    err << emit_indent();
    err << emit_print(".clock(clock),");
    err << emit_newline();
    trailing_comma = true;
  }

  if (component_mod->input_signals.size()) {
    err << emit_indent();
    err << emit_print("// Input signals");
    err << emit_newline();
    for (auto f : component_mod->input_signals) {
      err << emit_indent();
      err << emit_print(".%s(%s_%s),", f->cname(), inst_name.c_str(), f->cname());
      err << emit_newline();
      trailing_comma = true;
    }
  }

  if (component_mod->output_signals.size()) {
    err << emit_indent();
    err << emit_print("// Output signals");
    err << emit_newline();
    for (auto f : component_mod->output_signals) {
      err << emit_indent();
      err << emit_print(".%s(%s_%s),", f->cname(), inst_name.c_str(), f->cname());
      err << emit_newline();
      trailing_comma = true;
    }
  }

  if (component_mod->output_registers.size()) {
    err << emit_indent();
    err << emit_print("// Output registers");
    err << emit_newline();
    for (auto f : component_mod->output_registers) {
      err << emit_indent();
      err << emit_print(".%s(%s_%s),", f->cname(), inst_name.c_str(), f->cname());
      err << emit_newline();
      trailing_comma = true;
    }
  }

  for (auto m : component_mod->all_methods) {
    if (m->is_constructor()) continue;
    if (m->is_public() && m->internal_callers.empty()) {

      if (m->param_nodes.size() || m->has_return()) {
        err << emit_indent();
        err << emit_print("// %s() ports", m->cname());
        err << emit_newline();
      }

      int param_count = m->param_nodes.size();
      for (int i = 0; i < param_count; i++) {
        auto param = m->param_nodes[i];
        auto node_type = param.get_field(field_type);
        auto node_decl = param.get_field(field_declarator);

        err << emit_indent();
        err << emit_print(".%s_%s(%s_%s_%s),", m->cname(), node_decl.text().c_str(),
                          inst_name.c_str(), m->cname(), node_decl.text().c_str());
        err << emit_newline();
        trailing_comma = true;
      }

      if (m->has_return()) {
        auto node_type = m->_node.get_field(field_type);
        auto node_decl = m->_node.get_field(field_declarator);
        auto node_name = node_decl.get_field(field_declarator);

        err << emit_indent();
        err << emit_print(".%s_ret(%s_%s_ret),", m->cname(),
                          inst_name.c_str(), m->cname());
        err << emit_newline();
        trailing_comma = true;
      }
    }
  }

  // Remove trailing comma from port list
  if (trailing_comma) {
    err << emit_backspace();
    err << emit_backspace();
    err << emit_newline();
    trailing_comma = false;
  }

  indent_stack.pop_back();

  err << emit_indent();
  err << emit_print(")");
  err << emit_text(node_semi);
  err << emit_newline();

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_field_as_component(MnNode n) {
  Err err = emit_ws_to(n);

  // FIXME loop style?

  std::string type_name = n.type5();
  auto component_mod = lib->get_module(type_name);

  auto node_type = n.child(0);  // type
  auto node_decl = n.child(1);  // decl
  auto node_semi = n.child(2);  // semi

  auto inst_name = node_decl.text();

  // Swap template arguments with the values from the template instantiation.
  std::map<std::string, std::string> replacements;

  auto args = node_type.get_field(field_arguments);
  if (args) {
    int arg_count = args.named_child_count();
    for (int i = 0; i < arg_count; i++) {
      auto key = component_mod->all_modparams[i]->name();
      auto val = args.named_child(i).text();
      replacements[key] = val;
    }
  }

  err << emit_component_port_list(n);

  err << emit_submod_binding_fields(n);
  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Emits the fields that come after a submod declaration

// module my_mod(
//   .foo(my_mod_foo)
// );
// logic my_mod_foo; <-- this part

CHECK_RETURN Err MtCursor::emit_submod_binding_fields(MnNode component_decl) {
  Err err;

  if (current_mod->components.empty()) {
    return err;
  }

  assert(at_newline);

  auto component_type_node = component_decl.child(0);  // type
  auto component_name_node = component_decl.child(1);  // decl
  auto component_semi_node = component_decl.child(2);  // semi

  auto component_name = component_name_node.text();
  auto component_cname = component_name.c_str();

  std::string type_name = component_type_node.type5();
  auto component_mod = lib->get_module(type_name);

  // Swap template arguments with the values from the template
  // instantiation.
  std::map<std::string, std::string> replacements;

  auto args = component_type_node.get_field(field_arguments);
  if (args) {
    int arg_count = args.named_child_count();
    for (int i = 0; i < arg_count; i++) {
      auto key = component_mod->all_modparams[i]->name();
      auto val = args.named_child(i).text();
      replacements[key] = val;
    }
  }

  for (auto n : component_mod->input_signals) {
    // field_declaration
    auto output_type = n->get_type_node();
    auto output_decl = n->get_decl_node();

    MtCursor subcursor(lib, component_mod->source_file, component_mod, str_out);
    subcursor.echo = echo;
    subcursor.id_replacements = replacements;
    subcursor.cursor = output_type.start();

    err << emit_indent();
    err << subcursor.emit_type(output_type);
    err << subcursor.emit_ws();
    err << emit_print("%s_", component_cname);
    err << subcursor.emit_identifier(output_decl);
    err << prune_trailing_ws();
    err << emit_print(";");
    err << emit_newline();
  }

  for (auto n : component_mod->input_method_params) {
    // field_declaration
    auto output_type = n->get_type_node();
    auto output_decl = n->get_decl_node();

    MtCursor subcursor(lib, component_mod->source_file, component_mod, str_out);
    subcursor.echo = echo;
    subcursor.id_replacements = replacements;
    subcursor.cursor = output_type.start();

    err << emit_indent();
    err << subcursor.emit_type(output_type);
    err << subcursor.emit_ws();
    err << emit_print("%s_%s_", component_cname, n->func_name.c_str());
    err << subcursor.emit_identifier(output_decl);
    err << prune_trailing_ws();
    err << emit_print(";");
    err << emit_newline();
  }

  for (auto n : component_mod->output_signals) {
    // field_declaration
    auto output_type = n->get_type_node();
    auto output_decl = n->get_decl_node();

    MtCursor subcursor(lib, component_mod->source_file, component_mod, str_out);
    subcursor.echo = echo;
    subcursor.id_replacements = replacements;
    subcursor.cursor = output_type.start();

    err << emit_indent();
    err << subcursor.emit_type(output_type);
    err << subcursor.emit_ws();
    err << emit_print("%s_", component_cname);
    err << subcursor.emit_identifier(output_decl);
    err << prune_trailing_ws();
    err << emit_print(";");
    err << emit_newline();
  }

  for (auto n : component_mod->output_registers) {
    // field_declaration
    auto output_type = n->get_type_node();
    auto output_decl = n->get_decl_node();

    MtCursor subcursor(lib, component_mod->source_file, component_mod, str_out);
    subcursor.echo = echo;
    subcursor.id_replacements = replacements;
    subcursor.cursor = output_type.start();

    err << emit_indent();
    err << subcursor.emit_type(output_type);
    err << subcursor.emit_ws();
    err << emit_print("%s_", component_cname);
    err << subcursor.emit_identifier(output_decl);
    err << prune_trailing_ws();
    err << emit_print(";");
    err << emit_newline();
  }

  for (auto m : component_mod->output_method_returns) {
    auto getter_type = m->_node.get_field(field_type);
    auto getter_decl = m->_node.get_field(field_declarator);
    auto getter_name = getter_decl.get_field(field_declarator);

    MtCursor sub_cursor(lib, component_mod->source_file, component_mod,
                        str_out);
    sub_cursor.echo = echo;
    sub_cursor.id_replacements = replacements;

    sub_cursor.cursor = getter_type.start();

    err << emit_indent();
    err << sub_cursor.skip_ws();
    err << sub_cursor.emit_type(getter_type);
    err << sub_cursor.emit_ws();
    err << emit_print("%s_", component_cname);
    err << sub_cursor.emit_identifier(getter_name);
    err << prune_trailing_ws();
    err << emit_print("_ret;");
    err << emit_newline();
  }

  return err;
}

//------------------------------------------------------------------------------
// enum { FOO, BAR } e; => enum { FOO, BAR } e;

CHECK_RETURN Err MtCursor::emit_anonymous_enum(MnNode n) {
  Err err = emit_ws_to(n);

  for (auto child : n) {
    switch (child.field) {
      case field_type:
        override_size = 32;
        err << emit_type(child);
        override_size = 0;
        break;
      case field_declarator:
        err << emit_declarator(child);
        break;
      default:
        err << emit_default(child);
        break;
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// enum e { FOO, BAR }; => typedef enum { FOO, BAR } e;

CHECK_RETURN Err MtCursor::emit_simple_enum(MnNode n) {
  Err err = emit_ws_to(n);

  MnNode node_lit;
  MnNode node_name;

  for (auto child : n) {
    if (child.field == field_type) {
      node_name = child.get_field(field_name);
      err << emit_replacement(child, "typedef enum");
    } else if (child.field == field_default_value) {
      override_size = 32;
      err << emit_sym_initializer_list(child);
      override_size = 0;
    } else if (child.sym == anon_sym_SEMI) {
      err << emit_replacement(child, " %s;", node_name.text().c_str());
    } else if (child.sym == 65535) {
      // Declaring an enum class with a type is currently broken in TreeSitter.
      // This broken node contains the type info, pull it out manually.
      for (auto gchild : child) {
        if (gchild.sym == anon_sym_COLON) {
          err << skip_ws();
          err << skip_over(gchild);
        } else if (gchild.sym == sym_primitive_type) {
          err << emit_type(gchild);
        } else {
          err << emit_default(gchild);
        }
      }
      cursor = child.end();
    } else {
      err << emit_default(child);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_broken_enum(MnNode n) {
  Err err = emit_ws_to(sym_field_declaration, n);

  auto node_type = n.get_field(field_type);

  MnNode node_bitfield;
  for (auto c : n)
    if (c.sym == sym_bitfield_clause) node_bitfield = c;

  MnNode node_compound;
  for (auto c : node_bitfield)
    if (c.sym == sym_compound_literal_expression) node_compound = c;

  auto node_type_args = node_compound.get_field(field_type)
                            .get_field(field_scope)
                            .get_field(field_arguments);

  int enum_width = 0;
  for (auto c : node_type_args) {
    if (c.sym == sym_number_literal) {
      enum_width = atoi(c.start());
    }
  }

  auto node_name = node_type.get_field(field_name);
  auto node_vals = node_compound.get_field(field_value);

  if (!enum_width) err << ERR("Enum is 0 bits wide?");

  err << emit_print("typedef enum logic[%d:0] ", enum_width - 1);

  override_size = enum_width;
  push_cursor(node_vals);
  err << emit_sym_initializer_list(node_vals);
  pop_cursor(node_vals);
  err << emit_print(" ");
  override_size = 0;

  push_cursor(node_name);
  err << emit_identifier(node_name);
  pop_cursor(node_name);
  err << emit_print(";");

  cursor = n.end();
  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_enum(MnNode n) {
  Err err = emit_ws_to(n);
  auto node_type = n.get_field(field_type);
  assert(node_type.sym == sym_enum_specifier);

  // TreeSitter is broken for "enum foo : logic<8>::BASE {}".
  // Work around it by manually extracting the required field
  MnNode node_bitfield;
  for (auto c : n) {
    if (c.sym == sym_bitfield_clause) node_bitfield = c;
  };

  if (node_bitfield) {
    err << emit_broken_enum(n);
  } else if (node_type.get_field(field_name)) {
    err << emit_simple_enum(n);
  } else {
    err << emit_anonymous_enum(n);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_type_definition(MnNode node) {
  Err err = emit_ws_to(sym_type_definition, node);

  for (auto child : node) {
    switch(child.field) {
      case field_type:       err << emit_type(child); break;
      case field_declarator: err << emit_declarator(child); break;
      default:               err << emit_default(child); break;
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_type(MnNode node) {
  Err err = emit_ws_to(node);

  switch (node.sym) {
    case sym_qualified_identifier:
      // does this belong here?
      // yes, for std::string and namespaced types
      err << emit_sym_qualified_identifier(node);
      break;
    case sym_type_descriptor:
      err << emit_type(node.get_field(field_type));
      break;
    case sym_template_type:
      err << emit_sym_template_type(node);
      break;
    case sym_primitive_type:
      err << emit_sym_primitive_type(node);
      break;
    case sym_sized_type_specifier:
      err << emit_sym_sized_type_specifier(node);
      break;
    case alias_sym_type_identifier:
      err << emit_sym_type_identifier(node);
      break;
    case sym_type_definition:
      err << emit_sym_type_definition(node);
      break;
    case sym_struct_specifier:
      err << emit_sym_struct_specifier(node);
      break;
    case sym_template_declaration:
      err << emit_sym_template_declaration(node);
      break;
    case sym_class_specifier:
      err << emit_sym_class_specifier(node);
      break;
    case sym_enum_specifier:
      err << emit_sym_enum_specifier(node);
      break;
    default:
      err << emit_default(node);
      break;
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_identifier(MnNode node) {
  Err err = emit_ws_to(node);

  switch (node.sym) {
    case alias_sym_namespace_identifier:
      err << emit_text(node);
      break;
    case alias_sym_type_identifier:
      err << emit_sym_type_identifier(node);
      break;
    case sym_identifier:
      err << emit_sym_identifier(node);
      break;
    case alias_sym_field_identifier:
      err << emit_sym_field_identifier(node);
      break;
    case sym_qualified_identifier:
      err << emit_sym_qualified_identifier(node);
      break;
    default:
      err << emit_default(node);
      break;
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_init_declarator(MnNode node, bool elide_value) {
  Err err = emit_ws_to(node);

  for (auto child : node) {
    if (child.field == field_declarator) {
      err << emit_identifier(child);
      if (elide_value) {
        cursor = node.end();
        break;
      }
    }
    else if (child.field == field_value) {
      if (elide_value) {
        err << skip_over(child);
        err << skip_ws();
      }
      else {
        err << emit_expression(child);
      }
    }
    else {
      err << emit_default(child);
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// FIXME break these out

CHECK_RETURN Err MtCursor::emit_declarator(MnNode node, bool elide_value) {
  Err err = emit_ws_to(node);

  switch (node.sym) {
    case alias_sym_type_identifier:
      err << emit_sym_type_identifier(node);
      break;
    case sym_identifier:
    case alias_sym_field_identifier:
      err << emit_identifier(node);
      break;
    case sym_init_declarator:
      err << emit_sym_init_declarator(node, elide_value);
      break;
    case sym_array_declarator:
      err << emit_identifier(node.child(0));
      err << emit_text(node.child(1));
      err << emit_expression(node.child(2));
      err << emit_text(node.child(3));
      break;
    case sym_function_declarator:
      err << emit_declarator(node.get_field(field_declarator));
      err << emit_sym_parameter_list(node.get_field(field_parameters));

      // FIXME wat? what was this for?
      if (node.child_count() == 3) {
        // err << emit_sym_type_qualifier(node.child(2));
        err << skip_over(node.child(2));
      }
      break;
    case sym_pointer_declarator: {
      auto decl2 = node.get_field(field_declarator);
      cursor = decl2.start();
      err << emit_declarator(decl2, elide_value);
      break;
    }
    default:
      err << emit_default(node);
      break;
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// FIXME break these out

CHECK_RETURN Err MtCursor::emit_parameter_declaration(MnNode node) {
  Err err = emit_ws_to(node);
  assert(node.sym == sym_parameter_declaration);

  err << emit_type(node.child(0));
  err << emit_identifier(node.child(1));

  return err << check_done(node);
}

CHECK_RETURN Err MtCursor::emit_optional_parameter_declaration(MnNode node) {
  Err err = emit_ws_to(node);
  assert(node.sym == sym_optional_parameter_declaration);

  err << emit_type(node.child(0));
  err << emit_identifier(node.child(1));
  err << emit_text(node.child(2));
  err << emit_expression(node.child(3));

  return err << check_done(node);
}

CHECK_RETURN Err MtCursor::emit_module_parameter_declaration(MnNode node) {
  Err err = emit_ws_to(node);
  assert(node.sym == sym_optional_parameter_declaration);

  //err << emit_type(node.child(0));
  //err << skip_over(node.child(0));
  //err << emit_identifier(node.child(1));
  //err << emit_text(node.child(2));
  //err << emit_expression(node.child(3));

  auto param_type = node.get_field(field_type);
  auto param_decl = node.get_field(field_declarator);
  auto param_val  = node.get_field(field_default_value);

  /*
  cursor = param_type.start();
  err << emit_type(param_type);
  err << emit_print(" =$= ");
  */
  cursor = param_decl.start();
  err << emit_declarator(param_decl);
  err << emit_print(" = ");
  cursor = param_val.start();
  err << emit_expression(param_val);

  cursor = node.end();

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Emit field declarations. For components, also emit glue declarations and
// append the glue parameter list to the field.

// field_declaration = { type:type_identifier, declarator:field_identifier+ }
// field_declaration = { type:template_type,   declarator:field_identifier+ }
// field_declaration = { type:enum_specifier,  bitfield_clause (TREESITTER BUG)
// }

CHECK_RETURN Err MtCursor::emit_sym_field_declaration(MnNode n) {
  Err err = emit_ws_to(n);
  assert(n.sym == sym_field_declaration);

  // Struct outside of class
  if (current_mod == nullptr) {
    auto node_type = n.get_field(field_type);
    auto node_decl = n.get_field(field_declarator);
    auto node_semi = n.child(2);

    err << emit_type(node_type);
    err << emit_declarator(node_decl);
    err << emit_text(node_semi);
    return err << check_done(n);
  }

  // Const local variable
  if (n.is_const()) {
    auto node_static = n.child(0);
    auto node_const = n.child(1);
    auto node_type = n.child(2);
    auto node_decl = n.child(3);
    auto node_equals = n.child(4);
    auto node_value = n.child(5);
    auto node_semi = n.child(6);

    // err << emit_sym_storage_class_specifier(node_static);
    // err << emit_sym_type_qualifier(node_const);
    err << emit_print("localparam ");
    cursor = node_type.start();
    err << emit_type(node_type);
    err << emit_identifier(node_decl);
    err << emit_text(node_equals);
    err << emit_expression(node_value);
    err << emit_text(node_semi);
    return err;
  }

  // Enum
  if (n.get_field(field_type).sym == sym_enum_specifier) {
    return emit_enum(n);
  }

  //----------
  // Actual fields

  auto field = current_mod->get_field(n.name4());
  assert(field);

  if (field->is_component()) {
    // Component
    err << emit_field_as_component(n);
    return err << check_done(n);
  }
  else if (field->is_port()) {
    // Ports don't go in the class body.
    err << skip_over(n);
    return err << check_done(n);
  }
  else if (field->is_dead()) {
    err << comment_out(n);
    return err << check_done(n);
  }
  else {
    for (auto c : n) {
      switch(c.field) {
        case field_type:       err << emit_type(c); break;
        case field_declarator: err << emit_declarator(c); break;
        default:               err << emit_default(c); break;
      }
    }
    return err << check_done(n);
  }
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_struct_specifier(MnNode n) {
  Err err = emit_ws_to(n);
  assert(n.sym == sym_struct_specifier);

  // FIXME loop style
  // FIXME - Do we _have_ to mark it as packed?

  auto node_name = n.get_field(field_name);
  auto node_body = n.get_field(field_body);

  if (node_name) {
    // struct Foo {};

    err << emit_print("typedef ");
    err << emit_text(n.child(0));
    err << emit_print(" packed ");
    cursor = node_body.start();
    err << emit_sym_field_declaration_list(node_body, true);
    err << emit_print(" ");

    push_cursor(node_name);
    err << emit_identifier(node_name);
    pop_cursor(node_name);

    err << emit_print(";");
    cursor = n.end();
  } else {
    // typedef struct {} Foo;

    err << emit_text(n.child(0));
    err << emit_print(" packed ");
    err << emit_sym_field_declaration_list(node_body, true);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_param_port(MtMethod* m, MnNode node_type, MnNode node_name) {
  Err err;

  MtCursor sub_cursor = *this;
  sub_cursor.cursor = node_type.start();
  err << emit_indent();
  err << emit_print("input ");
  err << sub_cursor.emit_type(node_type);
  err << sub_cursor.emit_ws();
  err << sub_cursor.emit_print("%s_", m->cname());
  err << sub_cursor.emit_identifier(node_name);
  err << emit_print(",");
  err << emit_newline();

  trailing_comma = true;

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_param_binding(MtMethod* m, MnNode node_type, MnNode node_name) {
  Err err;

  MtCursor sub_cursor = *this;
  sub_cursor.cursor = node_type.start();
  err << emit_indent();
  err << sub_cursor.emit_type(node_type);
  err << sub_cursor.emit_ws();
  err << sub_cursor.emit_print("%s_", m->cname());
  err << sub_cursor.emit_identifier(node_name);
  err << emit_print(";");
  err << emit_newline();

  trailing_comma = true;

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_return_port(MtMethod* m, MnNode node_type, MnNode node_name) {
  Err err;

  MtCursor sub_cursor = *this;
  sub_cursor.cursor = node_type.start();
  err << emit_indent();
  err << emit_print("output ");
  err << sub_cursor.emit_type(node_type);
  err << sub_cursor.emit_ws();
  err << sub_cursor.emit_print("%s_ret", m->cname());
  err << emit_print(",");
  err << emit_newline();

  trailing_comma = true;

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_return_binding(MtMethod* m, MnNode node_type, MnNode node_name) {
  Err err;

  MtCursor sub_cursor = *this;
  sub_cursor.cursor = node_type.start();
  err << emit_indent();
  err << sub_cursor.emit_type(node_type);
  err << sub_cursor.emit_ws();
  err << sub_cursor.emit_print("%s_ret", m->cname());
  err << emit_print(";");
  err << emit_newline();

  trailing_comma = true;

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_method_ports(MtMethod* m) {
  Err err;

  if (m->param_nodes.size() || m->has_return()) {
    err << emit_indent();
    err << emit_print("// %s() ports", m->cname());
    err << emit_newline();
  }

  int param_count = m->param_nodes.size();
  for (int i = 0; i < param_count; i++) {
    auto param = m->param_nodes[i];
    auto node_type = param.get_field(field_type);
    auto node_decl = param.get_field(field_declarator);
    err << emit_param_port(m, node_type, node_decl);
  }

  if (m->has_return()) {
    auto node_type = m->_node.get_field(field_type);
    auto node_decl = m->_node.get_field(field_declarator);
    auto node_name = node_decl.get_field(field_declarator);
    err << emit_return_port(m, node_type, node_name);
  }

  return err;
}

//----------------------------------------

CHECK_RETURN Err MtCursor::emit_func_binding_vars(MtMethod* m) {
  Err err;

  int param_count = m->param_nodes.size();
  for (int i = 0; i < param_count; i++) {
    auto param = m->param_nodes[i];
    auto node_type = param.get_field(field_type);
    auto node_decl = param.get_field(field_declarator);
    err << emit_param_binding(m, node_type, node_decl);
  }

  if (m->has_return()) {
    auto node_type = m->_node.get_field(field_type);
    auto node_decl = m->_node.get_field(field_declarator);
    auto node_name = node_decl.get_field(field_declarator);
    err << emit_return_binding(m, node_type, node_name);
  }

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_field_port(MtField* f) {
  Err err;

  if (!f->is_public()) return err;
  if (f->is_component()) return err;

  err << emit_indent();

  if (f->is_public() && f->is_input()) {
    err << emit_print("input ");
  } else if (f->is_public() && f->is_signal()) {
    err << emit_print("output ");
  } else if (f->is_public() && f->is_register()) {
    err << emit_print("output ");
  } else {
    err << ERR("Unknown field port type for %s\n", f->cname());
  }

  if (err.has_err()) return err;

  auto node_type = f->get_type_node();
  auto node_decl = f->get_decl_node();

  MtCursor sub_cursor = *this;
  sub_cursor.cursor = node_type.start();
  err << sub_cursor.emit_type(node_type);
  err << sub_cursor.emit_identifier(node_decl);

  err << emit_print(",");
  err << emit_newline();

  trailing_comma = true;

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_module_parameter_list(MnNode param_list) {
  Err err;

  int param_count = 0;

  for (const auto& c : param_list) {
    switch (c.sym) {
      case sym_optional_parameter_declaration:
      case sym_parameter_declaration:
        param_count++;
        break;
    }
  }

  /*
  if (current_mod->constructor) {
    param_count += current_mod->constructor->param_nodes.size();
  }
  */

  if (!param_count) return err;

  //----------

  //err << emit_indent();
  //err << emit_print("#(");
  //err << emit_newline();

  // FIXME handle default template params
  if (param_list) {
    MtCursor sub_cursor = *this;
    sub_cursor.cursor = param_list.start();
    for (const auto& c : param_list) {
      switch (c.sym) {
        case sym_optional_parameter_declaration:
          err << sub_cursor.emit_print("parameter ");
          err << sub_cursor.skip_ws();
          err << sub_cursor.emit_module_parameter_declaration(c);
          err << sub_cursor.emit_print(";");
          err << sub_cursor.emit_newline();
          err << sub_cursor.emit_indent();
          break;

        case sym_parameter_declaration:
          err << ERR("Parameter '%s' must have a default value\n", c.text().c_str());
          break;

        default:
          err << sub_cursor.skip_over(c);
          break;
      }
    }
  }

  /*
  if (current_mod->constructor && current_mod->constructor->param_nodes.size()) {
    MtCursor sub_cursor = *this;
    for (const auto& c : current_mod->constructor->param_nodes) {
      err << sub_cursor.emit_print("parameter %s", c.name4().c_str());
      if (--param_count) err << sub_cursor.emit_print(",");
      err << sub_cursor.emit_newline();
    }
  }
  */

  //err << emit_print(")");
  //err << emit_newline();
  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_module_ports(MnNode class_body) {
  Err err;

  err << emit_indent();
  err << emit_print("(");
  err << emit_newline();

  // Save the indentation level of the struct body so we can use it in the
  // port list.
  push_indent(class_body);

  if (current_mod->needs_tick()) {
    err << emit_indent();
    err << emit_print("// global clock");
    err << emit_newline();
    err << emit_indent();
    err << emit_print("input logic clock,");
    err << emit_newline();
    trailing_comma = true;
  }

  if (current_mod->input_signals.size()) {
    err << emit_indent();
    err << emit_print("// input signals");
    err << emit_newline();
    for (auto f : current_mod->input_signals) {
      err << emit_field_port(f);
    }
  }

  if (current_mod->output_signals.size()) {
    err << emit_indent();
    err << emit_print("// output signals");
    err << emit_newline();
    for (auto f : current_mod->output_signals) {
      err << emit_field_port(f);
    }
  }

  if (current_mod->output_registers.size()) {
    err << emit_indent();
    err << emit_print("// output registers");
    err << emit_newline();
    for (auto f : current_mod->output_registers) {
      err << emit_field_port(f);
    }
  }

  for (auto m : current_mod->all_methods) {
    if (!m->is_init_ && m->is_public() && m->internal_callers.empty()) {
      err << emit_method_ports(m);
    }
  }
  // Remove trailing comma from port list
  if (trailing_comma) {
    err << emit_backspace();
    err << emit_backspace();
    err << emit_newline();
    trailing_comma = false;
  }

  pop_indent(class_body);
  err << emit_indent();
  err << emit_print(");");

  return err;
}

//------------------------------------------------------------------------------
// Change class/struct to module, add default clk/rst inputs, add input and
// ouptut ports to module param list.

CHECK_RETURN Err MtCursor::emit_sym_class_specifier(MnNode n) {
  Err err = emit_ws_to(sym_class_specifier, n);

  auto class_lit = n.child(0);
  auto class_name = n.get_field(field_name);
  auto class_body = n.get_field(field_body);

  auto old_mod = current_mod;
  current_mod = lib->get_module(class_name.text());
  assert(current_mod);

  //----------

  err << emit_indent();
  err << emit_replacement(class_lit, "module");
  err << emit_type(class_name);
  err << emit_print(" ");
  //err << emit_newline();

  err << emit_module_ports(class_body);

  push_indent(class_body);
  if (current_mod->mod_param_list) {
    err << emit_newline();
    err << emit_indent();
    err << emit_module_parameter_list(current_mod->mod_param_list);
  }
  err << emit_newline();
  err << emit_indent();
  err << emit_sym_field_declaration_list(class_body, false);
  pop_indent(class_body);

  cursor = n.end();
  current_mod = old_mod;

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_trigger_call(MtMethod* m) {
  Err err;

  if (m->has_return()) {
    err << emit_print("%s_ret = ", m->cname());
  }

  err << emit_print("%s(", m->cname());

  int param_count = m->param_nodes.size();
  for (int i = 0; i < param_count; i++) {
    auto param = m->param_nodes[i];
    err << emit_print("%s_%s", m->cname(),
                      param.get_field(field_declarator).text().c_str());
    if (i < param_count - 1) {
      err << emit_print(", ");
    }
  }
  err << emit_print(");");

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_param_as_field(MtMethod* method, MnNode n) {
  Err err;

  auto param_type = n.get_field(field_type);
  auto param_name = n.get_field(field_declarator);

  MtCursor subcursor(lib, current_source, current_mod, str_out);
  subcursor.echo = echo;

  err << emit_indent();
  subcursor.cursor = param_type.start();
  err << subcursor.emit_type(param_type);
  err << subcursor.emit_ws();

  err << emit_print("%s_", method->name().c_str());

  subcursor.cursor = param_name.start();
  err << subcursor.emit_identifier(param_name);

  err << prune_trailing_ws();
  err << emit_print(";");
  err << emit_newline();

  return err;
}

//------------------------------------------------------------------------------
// Emit the module body, with a few modifications.
// Discard the opening brace
// Replace the closing brace with "endmodule"

CHECK_RETURN Err MtCursor::emit_sym_field_declaration_list(MnNode n, bool is_struct) {
  Err err = emit_ws_to(sym_field_declaration_list, n);
  push_indent(n);

  bool convert = true;
  bool noconvert = false;
  bool exclude = false;
  bool dumpit = false;

  for (auto child : n) {
    if (child.sym == sym_comment && child.contains("metron_convert off")) convert = false;
    if (child.sym == sym_comment && child.contains("metron_convert on"))  convert = true;

    if (noconvert || !convert) {
      err << comment_out(child);
      noconvert = false;
      continue;
    }

    if (dumpit) {
      child.dump_tree();
      dumpit = false;
    }

    if (child.sym == sym_comment && child.contains("metron_noconvert")) noconvert = true;
    if (child.sym == sym_comment && child.contains("dumpit"))    dumpit = true;

    switch (child.sym) {
      case anon_sym_LBRACE:
        if (!is_struct) {
          err << skip_over(child);
        }
        else {
          err << emit_default(child);
        }
        break;
      case sym_access_specifier:
        err << comment_out(child);
        break;
      case sym_field_declaration:
        err << emit_sym_field_declaration(child);
        break;
      case sym_function_definition:
        err << emit_sym_function_definition(child);
        break;
      case anon_sym_RBRACE:
        err << emit_ws_to_newline();
        if (is_struct) {
          err << emit_default(child);
        }
        else {
          //err << emit_newline();
          //err << emit_trigger_calls();
          err << emit_replacement(child, "endmodule");
        }
        break;
      default:
        err << emit_default(child);
        break;
    }
  }

  pop_indent(n);
  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_expression_statement(MnNode node) {
  Err err = emit_ws_to(sym_expression_statement, node);

  if (node.child(0).sym == sym_call_expression) {
    if (can_omit_call(node.child(0))) {
      err << skip_over(node);
      return err;
    }
  }

  for (auto child : node) {
    switch (child.sym) {
      case sym_assignment_expression:
        err << emit_expression(child);
        break;
      case sym_call_expression:
        err << emit_expression(child);
        break;
      case sym_conditional_expression:
        err << emit_expression(child);
        break;
      case sym_update_expression:
        err << emit_expression(child);
        break;
      default:
        err << emit_default(child);
        break;
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_template_argument(MnNode node) {
  Err err = emit_ws_to(node);

  if (node.sym == sym_type_descriptor)
    err << emit_type(node);
  else if (node.is_expression())
    err << emit_expression(node);
  else
    err << emit_default(node);

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Change logic<N> to logic[N-1:0]

CHECK_RETURN Err MtCursor::emit_sym_template_type(MnNode n) {
  Err err = emit_ws_to(sym_template_type, n);

  err << emit_sym_type_identifier(n.get_field(field_name));
  auto args = n.get_field(field_arguments);

  bool is_logic = n.get_field(field_name).match("logic");
  if (is_logic) {
    auto logic_size = args.first_named_child();
    switch (logic_size.sym) {
      case sym_number_literal: {
        int width = atoi(logic_size.start());
        if (width > 1) {
          err << emit_replacement(args, "[%d:0]", width - 1);
        }
        cursor = args.end();
        break;
      }
      default:
        cursor = logic_size.start();
        err << emit_print("[");
        err << emit_template_argument(logic_size);
        err << emit_print("-1:0]");
        break;
    }
  } else {
    // Don't do this here, it needs to go with the rest of the args in the full param list
    //err << emit_sym_template_argument_list(args);
  }
  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Change <param, param> to #(param, param)

CHECK_RETURN Err MtCursor::emit_sym_template_argument_list(MnNode n) {
  Err err = emit_ws_to(sym_template_argument_list, n);

  // FIXME weird loop style

  for (auto c : n) {
    switch (c.sym) {
      case anon_sym_LT:
        err << emit_replacement(c, " #(");
        break;
      case anon_sym_GT:
        err << emit_replacement(c, ")");
        break;
      case anon_sym_COMMA:
        err << emit_text(c);
        break;
      default:
        err << emit_template_argument(c);
        break;
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Enum lists do _not_ turn braces into begin/end.

CHECK_RETURN Err MtCursor::emit_sym_enumerator(MnNode node) {
  Err err = emit_ws_to(sym_enumerator, node);

  for (auto child : node) {
    if (child.field == field_name) {
      err << emit_identifier(child);
    } else if (child.field == field_value) {
      err << emit_expression(child);
    } else {
      err << emit_default(child);
    }
  }

  return err << check_done(node);
}

CHECK_RETURN Err MtCursor::emit_sym_enumerator_list(MnNode node) {
  Err err = emit_ws_to(sym_enumerator_list, node);

  for (auto child : node) {
    if (child.sym == sym_enumerator)
      err << emit_sym_enumerator(child);
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_everything() {
  Err err;

  cursor = current_source->source;
  err << emit_sym_translation_unit(current_source->root_node);

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_toplevel_node(MnNode node) {
  Err err = emit_ws_to(node);

  switch (node.sym) {
    case sym_identifier:
      err << emit_identifier(node);
      break;

    case sym_preproc_def:
    case sym_preproc_if:
    case sym_preproc_ifdef:
    case sym_preproc_else:
    case sym_preproc_call:
    case sym_preproc_arg:
    case sym_preproc_include:
      err << emit_preproc(node);
      break;

    case sym_class_specifier:
    case sym_struct_specifier:
    case sym_type_definition:
    case sym_template_declaration:
      err << emit_type(node);
      break;

    case sym_namespace_definition:
      err << emit_sym_namespace_definition(node);
      break;

    case sym_expression_statement:
      if (node.text() != ";") {
        err << ERR(
            "Found unexpected expression statement in translation unit\n");
      }
      err << skip_over(node);
      break;

    case sym_declaration:
      err << emit_sym_declaration(node, false, false);
      break;

    case anon_sym_SEMI:
      err << skip_over(node);
      break;

    default:
      err << emit_default(node);
      break;
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_toplevel_block(MnNode n) {
  Err err = emit_ws_to(n);

  bool noconvert = false;
  bool dumpit = false;

  for (auto c : n) {
    if (noconvert) {
      noconvert = false;
      err << comment_out(c);
      err << check_done(c);
      continue;
    }

    if (dumpit) {
      c.dump_tree();
      dumpit = false;
    }

    if (c.sym == sym_comment && c.contains("metron_noconvert"))  noconvert = true;
    if (c.sym == sym_comment && c.contains("dumpit"))     dumpit = true;

    err << emit_toplevel_node(c);
    err << check_done(c);
  }

  // Toplevel blocks can have trailing whitespace that isn't contained by child
  // nodes.
  err << emit_ws();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Discard any trailing semicolons in the translation unit.

CHECK_RETURN Err MtCursor::emit_sym_translation_unit(MnNode n) {
  Err err = emit_ws_to(sym_translation_unit, n);

  err << emit_toplevel_block(n);

  if (cursor < current_source->source_end) {
    err << emit_span(cursor, current_source->source_end);
  }

  return err << emit_ws();
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_namespace_definition(MnNode n) {
  Err err = emit_ws_to(sym_namespace_definition, n);

  auto node_name = n.get_field(field_name);
  auto node_body = n.get_field(field_body);

  err << emit_print("package %s;", node_name.text().c_str());
  cursor = node_body.start();

  for (auto c : node_body) {
    switch (c.sym) {
      case anon_sym_LBRACE:
      case anon_sym_RBRACE:
        err << skip_over(c);
        break;
      case sym_struct_specifier:
      case sym_class_specifier:
      case sym_template_declaration:
        err << emit_type(c);
        break;
      case sym_namespace_definition:
        err << emit_sym_namespace_definition(c);
        break;
      case sym_declaration:
        // not sure what happens if we see an "int x = 1" in a namespace...
        err << emit_sym_declaration(c, false, false);
        break;
      default:
        err << emit_default(c);
        break;
    }
  }

  err << emit_print("endpackage");
  err << emit_indent();
  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Replace "0x" prefixes with "'h"
// Replace "0b" prefixes with "'b"
// Add an explicit size prefix if needed.

CHECK_RETURN Err MtCursor::emit_sym_number_literal(MnNode n, int size_cast) {
  Err err = emit_ws_to(sym_number_literal, n);

  assert(!override_size || !size_cast);
  if (override_size) size_cast = override_size;

  std::string body = n.text();

  // Count how many 's are in the number
  int spacer_count = 0;
  int prefix_count = 0;

  for (auto& c : body)
    if (c == '\'') {
      c = '_';
      spacer_count++;
    }

  if (body.starts_with("0x")) {
    prefix_count = 2;
    if (!size_cast) size_cast = ((int)body.size() - 2 - spacer_count) * 4;
    err << emit_print("%d'h", size_cast);
  } else if (body.starts_with("0b")) {
    prefix_count = 2;
    if (!size_cast) size_cast = (int)body.size() - 2 - spacer_count;
    err << emit_print("%d'b", size_cast);
  } else {
    if (size_cast) {
      err << emit_print("%d'd", size_cast);
    }
  }

  if (spacer_count) {
    err << emit_print(body.c_str() + prefix_count);
  } else {
    err << emit_span(n.start() + prefix_count, n.end());
  }

  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Change "return x" to "(funcname) = x" to match old Verilog return style.

CHECK_RETURN Err MtCursor::emit_sym_return(MnNode n) {
  Err err = emit_ws_to(sym_return_statement, n);

  for (auto c : n) {
    if (c.sym == anon_sym_return) {

      if (current_method->is_tock_) {
        assert(current_method->needs_binding || current_method->needs_ports);
        err << emit_replacement(c, "%s_ret =", current_method->name().c_str());
      }
      else if (current_method->is_func_ && current_method->is_public() && !current_method->called_in_module()) {
        assert(current_method->needs_binding || current_method->needs_ports);
        err << emit_replacement(c, "%s_ret =", current_method->name().c_str());
      }
      else if (current_method->is_tick_) {
        assert(current_method->needs_binding || current_method->needs_ports);
        err << emit_replacement(c, "%s_ret <=", current_method->name().c_str());
      }
      else {
        err << emit_replacement(c, "%s =", current_method->name().c_str());
      }
    } else if (c.is_expression()) {
      err << emit_expression(c);
    } else if (c.is_identifier()) {
      err << emit_identifier(c);
    } else {
      err << emit_default(c);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// FIXME translate types here

CHECK_RETURN Err MtCursor::emit_sym_primitive_type(MnNode n) {
  Err err = emit_ws_to(sym_primitive_type, n);

  err << emit_text(n);

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_identifier(MnNode n) {
  Err err = emit_ws_to(sym_identifier, n);

  auto name = n.name4();

  auto it = id_replacements.find(name);
  if (it != id_replacements.end()) {
    err << emit_replacement(n, it->second.c_str());
  } else if (preproc_vars.contains(name)) {
    err << emit_print("`");
    err << emit_text(n);
  } else if (name == "DONTCARE") {
    err << emit_replacement(n, "'x");
  } else {
    err << emit_text(n);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_type_identifier(MnNode n) {
  Err err = emit_ws_to(alias_sym_type_identifier, n);

  auto name = n.name4();
  auto it = id_replacements.find(name);
  if (it != id_replacements.end()) {
    err << emit_replacement(n, it->second.c_str());
  } else {
    err << emit_text(n);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_template_declaration(MnNode n) {
  Err err = emit_ws_to(sym_template_declaration, n);

  MnNode class_specifier;
  MnNode param_list;

  for (auto child : (MnNode)n) {
    if (child.sym == sym_template_parameter_list) {
      param_list = MnNode(child);
    }

    if (child.sym == sym_class_specifier) {
      class_specifier = MnNode(child);
    }
  }

  assert(!class_specifier.is_null());
  std::string class_name = class_specifier.get_field(field_name).text();

  auto old_mod = current_mod;
  current_mod = lib->get_module(class_name);

  cursor = class_specifier.start();
  err << emit_type(class_specifier);
  cursor = n.end();

  current_mod = old_mod;

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Replace foo.bar.baz with foo_bar_baz if the field refers to a submodule port,
// so that it instead refers to a glue expression.

CHECK_RETURN Err MtCursor::emit_sym_field_expression(MnNode n) {
  Err err = emit_ws_to(sym_field_expression, n);

  auto component_name = n.get_field(field_argument).text();
  auto component_field = n.get_field(field_field).text();

  auto component = current_mod->get_component(component_name);

  bool is_port = component && component->_type_mod->is_port(component_field);
  //printf("is port %s %d\n", component_field.c_str(), is_port);

  is_port = component && component->_type_mod->is_port(component_field);
  // FIXME needs to be || is_argument

  bool is_port_arg = false;
  if (current_method && (current_method->emit_as_always_comb || current_method->emit_as_always_ff)) {
    for (auto c : current_method->param_nodes) {
      if (c.get_field(field_declarator).text() == component_name) {
        is_port_arg = true;
        break;
      }
    }
  }

  if (component && is_port) {
    auto field = n.text();
    for (auto& c : field) {
      if (c == '.') c = '_';
    }
    err << emit_replacement(n, field.c_str());
  } else if (is_port_arg) {
    err << emit_print("%s_", current_method->name().c_str());
    err << emit_text(n);
  }
  else {
    // Local struct reference
    err << emit_text(n);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_case_statement(MnNode n) {
  Err err = emit_ws_to(sym_case_statement, n);

  bool anything_after_colon = false;
  int colon_hit = false;

  for (auto child : n) {
    if (child.sym == anon_sym_COLON) {
      colon_hit = true;
    } else {
      if (colon_hit) {
        if (child.sym != sym_comment) {
          anything_after_colon = true;
          break;
        }
      }
    }
  }

  for (auto child : n) {
    switch (child.sym) {
      case sym_break_statement:
        err << skip_over(child);
        break;
      case sym_identifier:
        err << emit_sym_identifier(child);
        break;
      case anon_sym_case:
        err << skip_over(child);
        err << skip_ws();
        break;
      case anon_sym_COLON:
        if (anything_after_colon) {
          err << emit_text(child);
        } else {
          err << emit_replacement(child, ",");
        }
        break;
      case sym_if_statement:
      case sym_compound_statement:
      case sym_expression_statement:
        err << emit_statement(child);
        break;
      case sym_qualified_identifier:
        err << emit_sym_qualified_identifier(child);
        break;
      default:
        err << emit_default(child);
        break;
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_switch_statement(MnNode node) {
  Err err = emit_ws_to(sym_switch_statement, node);

  for (auto child : node) {
    if (child.sym == anon_sym_switch) {
      err << emit_replacement(child, "case");
    }
    else if (child.field == field_condition)
      err << emit_sym_condition_clause(child);
    else if (child.field == field_body)
      err << emit_sym_compound_statement(child, "", "endcase");
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Unwrap magic /*#foo#*/ comments to pass arbitrary text to Verilog.

CHECK_RETURN Err MtCursor::emit_sym_comment(MnNode n) {
  Err err = emit_ws_to(sym_comment, n);

  auto body = n.text();
  if (body.starts_with("/*#") && body.ends_with("#*/")) {
    body.erase(body.size() - 3, 3);
    body.erase(0, 3);
    err << emit_replacement(n, body.c_str());
  } else {
    err << emit_text(n);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Verilog doesn't use "break"

CHECK_RETURN Err MtCursor::emit_sym_break_statement(MnNode n) {
  Err err = emit_ws_to(sym_break_statement, n);

  err << skip_over(n);

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// TreeSitter nodes slightly broken for "a = b ? c : d;"...

CHECK_RETURN Err MtCursor::emit_sym_conditional_expression(MnNode n) {
  Err err = emit_ws_to(sym_conditional_expression, n);

  for (auto child : n) {
    err << emit_expression(child);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Static variables become localparams at module level.

CHECK_RETURN Err MtCursor::emit_sym_storage_class_specifier(MnNode n) {
  Err err = emit_ws_to(sym_storage_class_specifier, n);

  err << skip_over(n);
  err << skip_ws();

  return err;
}

//------------------------------------------------------------------------------
// Change "std::string" to "string".
// Change "enum::val" to "val" (SV doesn't support scoped enum values)

CHECK_RETURN Err MtCursor::emit_sym_qualified_identifier(MnNode node) {
  Err err = emit_ws_to(sym_qualified_identifier, node);

  // FIXME loop style

  bool elide_scope = false;

  for (auto child : node) {
    if (child.field == field_scope) {
      if (child.text() == "std") elide_scope = true;
      if (current_mod->get_enum(child.text())) elide_scope = true;
    }
  }

  for (auto child : node) {
    if (child.field == field_scope) {
      if (elide_scope) {
        err << skip_over(child);
        err << skip_ws();
      }
      else {
        err << emit_identifier(child);
      }
    }
    else if (child.field == field_name) {
      err << emit_identifier(child);
    }
    else if (!child.is_named() && child.text() == "::" && elide_scope) {
      err << skip_over(child);
      err << skip_ws();
    }
    else {
      err << emit_default(child);
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

bool MtCursor::branch_contains_component_call(MnNode n) {
  if (n.sym == sym_call_expression) {
    if (n.get_field(field_function).sym == sym_field_expression) {
      return true;
    }
  }

  for (const auto& c : n) {
    if (branch_contains_component_call(c)) return true;
  }

  return false;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_statement(MnNode n) {
  Err err = emit_ws_to(n);

  switch (n.sym) {
    case sym_declaration:
      // type should be hoisted.
      err << emit_sym_declaration(n, true, false);
      break;
    case sym_if_statement:
      err << emit_sym_if_statement(n);
      break;
    case sym_compound_statement:
      err << emit_sym_compound_statement(n, "begin", "end");
      break;
    case sym_expression_statement:
      err << emit_sym_expression_statement(n);
      break;
    case sym_return_statement:
      err << emit_sym_return(n);
      break;
    case sym_switch_statement:
      err << emit_sym_switch_statement(n);
      break;
    case sym_using_declaration:
      err << emit_sym_using_declaration(n);
      break;
    case sym_case_statement:
      err << emit_sym_case_statement(n);
      break;
    case sym_break_statement:
      err << emit_sym_break_statement(n);
      break;
    default:
      err << emit_default(n);
      break;
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_expression(MnNode n) {
  Err err = emit_ws_to(n);

  switch (n.sym) {
    case sym_identifier:
    case sym_qualified_identifier:
      err << emit_identifier(n);
      break;

    case sym_type_descriptor:
      err << emit_type(n);
      break;

    case sym_subscript_expression:
      err << emit_child_expressions(n);
      break;
    case sym_binary_expression:
      err << emit_child_expressions(n);
      break;
    case sym_parenthesized_expression:
      err << emit_child_expressions(n);
      break;
    case sym_unary_expression:
      err << emit_child_expressions(n);
      break;

    case sym_field_expression:
      err << emit_sym_field_expression(n);
      break;
    case sym_call_expression:
      err << emit_sym_call_expression(n);
      break;
    case sym_assignment_expression:
      err << emit_sym_assignment_expression(n);
      break;
    case sym_conditional_expression:
      err << emit_sym_conditional_expression(n);
      break;
    case sym_update_expression:
      err << emit_sym_update_expression(n);
      break;
    case sym_nullptr:
      err << emit_replacement(n, "\"\"");
      break;
    case sym_initializer_list:
      err << emit_sym_initializer_list(n);
      break;


    default:
      err << emit_default(n);
      break;
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_condition_clause(MnNode node) {
  Err err = emit_ws_to(sym_condition_clause, node);

  for (auto child : node) {
    if (child.field == field_value)
      err << emit_expression(child);
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// If statements _must_ use {}, otherwise we have no place to insert component
// bindings if the branch contains a component method call.

CHECK_RETURN Err MtCursor::emit_sym_if_statement(MnNode node) {
  Err err = emit_ws_to(sym_if_statement, node);

  for (auto child : node) {
    switch (child.sym) {
      case sym_condition_clause:
        err << emit_sym_condition_clause(child);
        break;
      case sym_if_statement:
      case sym_compound_statement:
        err << emit_statement(child);
        break;
      case sym_expression_statement:
        if (branch_contains_component_call(child)) {
          return err << ERR("If branches that contain component calls must use {}.\n");
          err << skip_over(child);
        } else {
          err << emit_statement(child);
        }
        break;
      default:
        err << emit_default(child);
        break;
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Enums are broken.

CHECK_RETURN Err MtCursor::emit_sym_enum_specifier(MnNode node) {
  Err err = emit_ws_to(sym_enum_specifier, node);

  for (auto child : node) {
    if (child.field == field_name)
      err << emit_declarator(child);
    else if (child.field == field_body)
      err << emit_sym_enumerator_list(child);
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_using_declaration(MnNode n) {
  Err err = emit_ws_to(sym_using_declaration, n);

  // FIXME child index
  auto name = n.child(2).text();
  err << emit_replacement(n, "import %s::*;", name.c_str());

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_declaration(MnNode n, bool elide_type, bool elide_value) {
  Err err = emit_ws_to(sym_declaration, n);

  bool is_static = false;
  bool is_const = false;

  MnNode node_type;

  for (auto child : (MnNode)n) {
    if (child.sym == sym_storage_class_specifier && child.text() == "static")
      is_static = true;
    if (child.sym == sym_type_qualifier && child.text() == "const")
      is_const = true;

    if (child.field == field_type) node_type = child;
  }

  // Check for "static const char"
  if (is_const && node_type.text() == "char") {
    err << emit_print("localparam string ");
    auto init_decl = n.get_field(field_declarator);
    auto pointer_decl = init_decl.get_field(field_declarator);
    auto name = pointer_decl.get_field(field_declarator);
    cursor = name.start();
    err << emit_text(name);
    err << emit_print(" = ");

    auto val = init_decl.get_field(field_value);
    cursor = val.start();
    err << emit_text(val);
    err << prune_trailing_ws();
    err << emit_print(";");
    cursor = n.end();
    return err;
  }

  if (is_const) {
    err << emit_print("parameter ");
  }

  // Declarations inside scopes get their type removed, as the type+name decl
  // has been hoisted to the top of the scope.

  bool elide_decl = false;
  for (auto child : n) {
    if (child.field == field_declarator && child.is_identifier() && elide_type) {
      elide_decl = true;
    }
  }

  if (elide_decl) {
    err << skip_over(n);
    err << skip_ws();
  }
  else {
    for (auto child : n) {
      if (child.sym == sym_storage_class_specifier)
        err << skip_over(child) << skip_ws();
      else if (child.sym == sym_type_qualifier) {
        err << skip_over(child) << skip_ws();
      }
      else if (child.field == field_type) {
        if (elide_type) {
          err << skip_over(child) << skip_ws();
        }
        else {
          err << emit_type(child);
        }
      }
      else if (child.field == field_declarator) {
        err << emit_declarator(child, elide_value);
      }
      else {
        err << emit_default(child);
      }
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// "unsigned int" -> "int unsigned"

CHECK_RETURN Err MtCursor::emit_sym_sized_type_specifier(MnNode n) {
  Err err = emit_ws_to(sym_sized_type_specifier, n);

  // FIXME loop style

  for (auto c : n) {
    if (c.field == field_type) {
      push_cursor(c);
      err << emit_type(c);
      err << emit_ws();
      cursor = c.end();
      pop_cursor(c);
      break;
    }
  }

  for (auto c : n) {
    if (c.field == field_type) {
      err << skip_over(c);
      err << skip_ws();
    }
    else {
      err << emit_default(c);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// Arg lists are the same in C and Verilog.

CHECK_RETURN Err MtCursor::emit_sym_argument_list(MnNode node) {
  Err err = emit_ws_to(sym_argument_list, node);

  for (auto child : node) {
    if (child.is_identifier())
      err << emit_identifier(child);
    else if (child.is_expression())
      err << emit_expression(child);
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_parameter_list(MnNode node) {
  Err err = emit_ws_to(sym_parameter_list, node);

  for (auto child : node) {
    if (child.sym == sym_parameter_declaration)
      err << emit_parameter_declaration(child);
    else if (child.sym == sym_optional_parameter_declaration)
      err << emit_optional_parameter_declaration(child);
    else
      err << emit_default(child);
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_field_identifier(MnNode n) {
  Err err = emit_ws_to(alias_sym_field_identifier, n);

  err << emit_text(n);

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// preproc_arg has leading whitespace for some reason

CHECK_RETURN Err MtCursor::emit_sym_preproc_arg(MnNode n) {
  Err err;
  //err << emit_text(n);

  // If we see any other #defined constants inside a #defined value,
  // prefix them with '`'
  std::string arg = n.text();
  std::string new_arg;

  for (int i = 0; i < arg.size(); i++) {
    for (const auto& def_pair : preproc_vars) {
      if (def_pair.first == arg) continue;
      if (strncmp(&arg[i], def_pair.first.c_str(), def_pair.first.size()) ==
          0) {
        new_arg.push_back('`');
        break;
      }
    }
    new_arg.push_back(arg[i]);
  }

  err << emit_replacement(n, new_arg.c_str());

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_preproc_ifdef(MnNode n) {
  Err err = emit_ws_to(sym_preproc_ifdef, n);

  for (auto child : n) {
    err << emit_toplevel_node(child);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

// This doesn't really work right, TreeSitter interprets "logic<8>" in "#define FOO logic<8>" as a template function call
#if 0

  {
    MtSourceFile dummy_source;
    dummy_source.lib = current_source->lib;
    dummy_source.source = arg.data();
    dummy_source.source_end = arg.data() + arg.size();
    dummy_source.lang = tree_sitter_cpp();
    dummy_source.parser = ts_parser_new();
    ts_parser_set_language(dummy_source.parser, dummy_source.lang);
    dummy_source.tree = ts_parser_parse_string(dummy_source.parser, NULL, arg.data(), (uint32_t)arg.size());

    TSNode ts_root = ts_tree_root_node(dummy_source.tree);
    auto root_sym = ts_node_symbol(ts_root);
    dummy_source.root_node = MnNode(ts_root, root_sym, 0, &dummy_source);

    if (dummy_source.root_node.sym == sym_translation_unit) {
      dummy_source.root_node = dummy_source.root_node.child(0);
    }

    // TreeSitter tacks an ERROR node on if we parse just a bare integer constant
    if (dummy_source.root_node.sym == 0xFFFF) {
      dummy_source.root_node = dummy_source.root_node.child(0);
    }

    //dummy_source.root_node.dump_tree();

    MtCursor preproc_cursor(current_source->lib, &dummy_source, nullptr, str_out);
    preproc_cursor.preproc_vars = preproc_vars;

    //dummy_source.root_node.dump_tree();
    if (dummy_source.root_node.is_expression()) {
      err << preproc_cursor.emit_expression(dummy_source.root_node);
    }
    else if (dummy_source.root_node.is_statement()) {
      err << preproc_cursor.emit_statement(dummy_source.root_node);
    }
    else {
      err << preproc_cursor.emit_default(dummy_source.root_node);
    }
  }
  cursor = n.end();
#endif

CHECK_RETURN Err MtCursor::emit_sym_preproc_def(MnNode n) {
  Err err = emit_ws_to(sym_preproc_def, n);

  MnNode node_name;

  for (auto child : n) {
    if (child.field == field_name) {
      err << emit_identifier(child);
      node_name = child;
    }
    else if (child.field == field_value) {
      preproc_vars[node_name.text()] = child;
      err << emit_sym_preproc_arg(child);
    }
    else {
      err << emit_default(child);
    }
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------
// FIXME need to do smarter stuff here...

CHECK_RETURN Err MtCursor::emit_preproc(MnNode n) {
  Err err = emit_ws_to(n);

  switch (n.sym) {
    case sym_preproc_def:
      err << emit_sym_preproc_def(n);
      break;
    case sym_preproc_if:
      err << skip_over(n);
      break;
    case sym_preproc_call:
      err << skip_over(n);
      break;
    case sym_preproc_arg:
      err << emit_sym_preproc_arg(n);
      break;
    case sym_preproc_include:
      err << emit_sym_preproc_include(n);
      break;
    case sym_preproc_ifdef:
      err << emit_sym_preproc_ifdef(n);
      break;
    default:
      n.error();
      break;
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_for_statement(MnNode node) {
  Err err = emit_ws_to(sym_for_statement, node);

  for (auto child : node) {
    if (child.sym == sym_declaration) {
      err << emit_sym_declaration(child, true, false);
    }
    else if (child.is_expression()) {
      err << emit_expression(child);
    }
    else if (child.is_statement()) {
      err << emit_statement(child);
    }
    else {
      err << emit_default(child);
    }
   }

  return err << check_done(node);
}

//------------------------------------------------------------------------------
// Emit the block with the correct type of "begin/end" pair, hoisting locals
// to the top of the body scope.

CHECK_RETURN Err MtCursor::emit_sym_compound_statement(
    MnNode node, const std::string& delim_begin, const std::string& delim_end) {
  Err err = emit_ws_to(sym_compound_statement, node);

  bool noconvert = false;
  bool dumpit = false;

  for (auto child : node) {

    if (noconvert) {
      noconvert = false;
      err << comment_out(child);
      continue;
    }

    if (dumpit) {
      child.dump_tree();
      dumpit = false;
    }

    if (child.sym == sym_comment && child.contains("metron_noconvert"))  noconvert = true;
    if (child.sym == sym_comment && child.contains("dumpit"))     dumpit = true;

    switch (child.sym) {
      case anon_sym_LBRACE:
        push_indent(node);
        err << emit_replacement(child, "%s", delim_begin.c_str());
        err << emit_ws_to_newline();
        err << emit_hoisted_decls(node);
        break;

      case sym_declaration:
        // We may need to insert input port bindings before any statement that
        // could include a call expression. We search the tree for calls and emit
        // those bindings here.
        err << emit_ws_to_newline();
        err << emit_call_arg_bindings(child);
        // type should be hoisted
        err << emit_sym_declaration(child, true, false);
        break;

      case sym_using_declaration:
        err << emit_sym_using_declaration(child);
        break;

      case sym_compound_statement:
        err << emit_statement(child);
        break;

      case sym_for_statement:
        err << emit_sym_for_statement(child);
        break;

      case sym_break_statement:
      case sym_if_statement:
      case sym_expression_statement:
      case sym_return_statement:
      case sym_switch_statement:
      case sym_case_statement:
        // We may need to insert input port bindings before any statement that
        // could include a call expression. We search the tree for calls and emit
        // those bindings here.
        if (!at_newline) err << emit_ws_to_newline();
        err << emit_call_arg_bindings(child);
        err << emit_statement(child);
        break;

      case anon_sym_RBRACE:
        pop_indent(node);
        err << emit_replacement(child, "%s", delim_end.c_str());
        break;

      default:
        err << emit_default(child);
        break;
    }
  }

  return err << check_done(node);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_sym_update_expression(MnNode n) {
  Err err = emit_ws_to(sym_update_expression, n);

  auto id = n.get_field(field_argument);
  auto op = n.get_field(field_operator);

  auto left_is_field = current_mod->get_field(id.name4()) != nullptr;

  if (n.get_field(field_operator).text() == "++") {
    push_cursor(id);
    err << emit_identifier(id);
    pop_cursor(id);
    if (current_method && current_method->is_tick_ && left_is_field) {
      err << emit_print(" <= ");
    }
    else{
      err << emit_print(" = ");
    }
    push_cursor(id);
    err << emit_identifier(id);
    pop_cursor(id);
    err << emit_print(" + 1");
  } else if (n.get_field(field_operator).text() == "--") {
    push_cursor(id);
    err << emit_identifier(id);
    pop_cursor(id);
    if (current_method && current_method->is_tick_ && left_is_field) {
      err << emit_print(" <= ");
    }
    else{
      err << emit_print(" = ");
    }
    push_cursor(id);
    err << emit_identifier(id);
    pop_cursor(id);
    err << emit_print(" - 1");
  } else {
    err << ERR("Unknown update expression %s\n", n.text().c_str());
  }

  cursor = n.end();

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::check_done(MnNode n) {
  Err err;
  if (cursor < n.end()) {
    err << ERR("Cursor was left inside the current node\n");
  }

  if (cursor > n.end()) {
    for (auto c = n.end(); c < cursor; c++) {
      if (!isspace(*c)) {
        err << ERR("Cursor ended up protruding into the next node\n");
      }
    }
  }

  return err;
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_child_expressions(MnNode n) {
  Err err = emit_ws_to(n);

  for (auto child : n) {
    err << emit_expression(child);
  }

  return err << check_done(n);
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtCursor::emit_default(MnNode node) {
  Err err = emit_ws_to(node);

  static std::map<std::string,std::string> keyword_map = {
    {"#ifdef",  "`ifdef"},
    {"#ifndef", "`ifndef"},
    {"#else",   "`else"},
    {"#elif",   "`elif"},
    {"#endif",  "`endif"},
    {"#define", "`define"},
    {"#undef",  "`undef"},
  };

  if (!node.is_named()) {
    auto it = keyword_map.find(node.text());
    if (it != keyword_map.end()) {
      err << emit_replacement(node, (*it).second.c_str());
    }
    else {
      err << emit_text(node);
    }
    return err;
  }

  if (node.is_literal()) {
    switch (node.sym) {
      case sym_string_literal:
        err << emit_text(node);
        break;
      case sym_number_literal:
        err << emit_sym_number_literal(node);
        break;
      default:
        // KCOV_OFF
        err << ERR("%s : No handler for %s\n", __func__, node.ts_node_type());
        node.error();
        break;
        // KCOV_ON
    }
    return err;
  }

  switch (node.sym) {
    case sym_comment:
      err << emit_sym_comment(node);
      break;
    default:
      // KCOV_OFF
      err << ERR("%s : No handler for %s\n", __func__, node.ts_node_type());
      node.error();
      break;
      // KCOV_ON
  }

  return err << check_done(node);
}
