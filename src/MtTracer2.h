#pragma once

#include "Err.h"
#include "Platform.h"
#include "MtInstance.h"
#include "MtUtils.h"

struct MtModLibrary;
struct MtField;
struct MtMethod;
struct MnNode;

class MtTracer2 {
public:
  MtTracer2(MtModLibrary* lib, MtModuleInstance* root_inst, bool verbose);

  CHECK_RETURN Err log_action(MtFieldInstance* field_inst, TraceAction action, SourceRange source);

  CHECK_RETURN Err trace_method(MtMethod* method);

  CHECK_RETURN Err trace_identifier(MtMethodInstance* inst, MnNode node, TraceAction action);
  CHECK_RETURN Err trace_declarator(MtMethodInstance* inst, MnNode node);
  CHECK_RETURN Err trace_statement (MtMethodInstance* inst, MnNode node);
  CHECK_RETURN Err trace_default   (MtMethodInstance* inst, MnNode node);

  CHECK_RETURN Err trace_sym_function_definition(MtMethodInstance* inst, MnNode node);
  CHECK_RETURN Err trace_sym_compound_statement (MtMethodInstance* inst, MnNode node);
  CHECK_RETURN Err trace_sym_declaration        (MtMethodInstance* inst, MnNode node);

  std::vector<MtInstance*> path;

  MtModLibrary* lib;
  MtModuleInstance* root_inst;
  bool verbose;
};
