/*
 * Copyright 2015 Sam Payson. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

namespace flatbuffers {
namespace rust {



static bool IsEnum(const Type &type) {
  return IsScalar(type.base_type) && type.enum_def;
}

static bool IsBool(const Type &type) {
  return type.base_type == BASE_TYPE_BOOL;
}

// Ensure that a type is prefixed with its module whenever it is used
// outside of its module.
//
// TODO: Is this necessary?
static std::string WrapInModule(const Parser &parser, const Namespace *ns,
                                const std::string &name) {
  if (parser.namespaces_.back() != ns) {
    std::string qualified_name = "::";
    for (auto it = ns->components.begin();
             it != ns->components.end(); ++it) {
      qualified_name += *it + "::";
    }
    return qualified_name + name;
  } else {
    return name;
  }
}

static std::string WrapInModule(const Parser &parser,
                                const Definition &def) {
  return WrapInModule(parser, def.defined_namespace, def.name);
}

// Return a Rust type from the table in idl.h
static std::string GenTypeBasic(const Parser &parser, const Type &type,
                                bool real_enum) {
  static const char *rstypename[] = {
    #define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, JTYPE, GTYPE, NTYPE, RTYPE) #RTYPE,
      FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
    #undef FLATBUFFERS_TD
  };
  return real_enum && type.enum_def
      ? WrapInModule(parser, *type.enum_def)
      : rstypename[type.base_type];
}


static std::string GenTypeWire(const Parser &parser, const Type &type,
                               const char *postfix, bool real_enum);

// Return a C++ pointer type, specialized to the actual struct/table types,
// and vector element types.
static std::string GenTypePointer(const Parser &parser, const Type &type) {
  switch (type.base_type) {
    case BASE_TYPE_STRING:
      return "fb::String";
    case BASE_TYPE_VECTOR:
      if (!IsScalar(type.VectorType().base_type) && !IsStruct(type.VectorType())) {
        return "fb::Vector<" +
          GenTypeWire(parser, type.VectorType(), "", false) + ", &" +
          GenTypePointer(parser, type.VectorType()) + ">";
      } else {
        return "fb::Vector<" +
          GenTypeWire(parser, type.VectorType(), "", false) + ">";
      }
    case BASE_TYPE_STRUCT: {
      return WrapInModule(parser, *type.struct_def);
    }
    case BASE_TYPE_UNION:
      // fall through
    default:
      return "()";
  }
}

// Return a C++ type for any type (scalar/pointer) specifically for
// building a flatbuffer.
static std::string GenTypeWire(const Parser &parser, const Type &type,
                               const char *postfix, bool real_enum) {
  return IsScalar(type.base_type)
    ? GenTypeBasic(parser, type, real_enum) + postfix
    : IsStruct(type)
      ? "&" + GenTypePointer(parser, type)
      : "fb::Offset<" + GenTypePointer(parser, type) + ">" + postfix;
}

// Return a C++ type for any type (scalar/pointer) that reflects its
// serialized size.
// static std::string GenTypeSize(const Parser &parser, const Type &type) {
//   return IsScalar(type.base_type)
//     ? GenTypeBasic(parser, type, false)
//     : IsStruct(type)
//       ? GenTypePointer(parser, type)
//       : "flatbuffers::UOffset";
// }

// Return a C++ type for any type (scalar/pointer) specifically for
// using a flatbuffer.
static std::string GenTypeGet(const Parser &parser, const Type &type,
                              const char *afterbasic, const char *beforeptr,
                              const char *afterptr, bool real_enum) {
  if (IsBool(type)) {
    return "bool";
  } else if (IsScalar(type.base_type)) {
    return GenTypeBasic(parser, type, real_enum) + afterbasic;
  } else {
    return beforeptr + GenTypePointer(parser, type) + afterptr;
  }
}

// Generate an enum declaration and an enum string lookup table.
static void GenEnum(const Parser &parser, EnumDef &enum_def,
                    std::string *code_ptr, std::string *code_ptr_post,
                    const GeneratorOptions &opts) {
  (void)parser;
  (void)opts;

  if (enum_def.generated) return;
  std::string &code = *code_ptr;
  std::string &code_post = *code_ptr_post;
  GenComment(enum_def.doc_comment, code_ptr, nullptr);
  code += "pub enum " + enum_def.name + " {\n";
  for (auto it = enum_def.vals.vec.begin();
       it != enum_def.vals.vec.end();
       ++it) {
    auto &ev = **it;
    GenComment(ev.doc_comment, code_ptr, nullptr, "  ");
    code += "  " + ev.name + " = " + NumToString(ev.value) + ",\n";
  }
  code += "}\n\n";

  code += "impl " + enum_def.name + " {\n";
  code += "    pub fn name(&self) -> &'static str {\n";
  code += "        use self::" + enum_def.name + "::*;\n\n";
  code += "        match *self {\n";

  for (auto it = enum_def.vals.vec.begin();
       it != enum_def.vals.vec.end();
       ++it) {
    auto &ev = **it;
    code += "            " + ev.name + " => \"" + ev.name + "\", \n";
  }
  code += "        }\n";
  code += "    }\n";
  code += "}\n\n";

  code += "impl ::num::FromPrimitive for " + enum_def.name + " {\n";
  code += "    fn from_i64(n: i64) -> Option<" + enum_def.name + "> {\n";
  code += "        use self::" + enum_def.name + "::*;\n\n";
  code += "        Some( match n {\n";

  for (auto it = enum_def.vals.vec.begin();
       it != enum_def.vals.vec.end();
       ++it) {
    auto &ev = **it;
    code += "            " + NumToString(ev.value) + " => " + ev.name + ",\n";
  }
  code += "            _ => return None,\n";
  code += "        })\n    \n}\n\n";

  code += "    fn from_u64(n: u64) -> Option<" + enum_def.name + "> {\n";
  code += "        ::num::FromPrimitive::from_i64(n as i64)\n";
  code += "    }\n}\n\n";
}

std::string GenUnderlyingCast(const FieldDef &field, bool from,
                              const std::string &val) {

  static const char *rstypename[] = {
    #define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, JTYPE, GTYPE, NTYPE, RTYPE) #RTYPE,
      FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
    #undef FLATBUFFERS_TD
  };

  if (field.value.type.enum_def && IsScalar(field.value.type.base_type)) {
    return from
      ? "::num::FromPrimitive::from_i64(" + val  + " as i64)"
      : val + " as " + rstypename[field.value.type.base_type];
  } else if (field.value.type.base_type == BASE_TYPE_BOOL) {
    return from
      ? val + " != 0"
      : "if " + val + " { 0u8 } else { 1u8 }";
  } else {
    return val;
  }
}

// Generate an accessor struct, builder structs & function for a table.
static void GenTable(const Parser &parser, StructDef &struct_def,
                     const GeneratorOptions &opts, std::string *code_ptr) {
  (void)opts;

  static const char *rstypename[] = {
    #define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, JTYPE, GTYPE, NTYPE, RTYPE) #RTYPE,
      FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
    #undef FLATBUFFERS_TD
  };

  if (struct_def.generated) return;
  std::string &code = *code_ptr;

  // Generate an accessor struct, with methods of the form
  // pub fn name(&self) -> type { self.inner.get_field(offset, defaultval); }

  GenComment(struct_def.doc_comment, code_ptr, nullptr);
  code += "pub struct " + struct_def.name + " {\n";
  code += "    inner: fb::Table,\n";
  code += "}\n\n";


  code += "impl " + struct_def.name + " {\n";
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    if (!field.deprecated) {  // Deprecated fields won't be accessible.
      GenComment(field.doc_comment, code_ptr, nullptr, "  ");
      code += "    pub fn " + field.name + "(&self) -> ";
      if (!IsScalar(field.value.type.base_type) || field.value.type.enum_def) {
          code += "Option<";
      }
      code += GenTypeGet(parser, field.value.type, "", "&", "", true);
      if (!IsScalar(field.value.type.base_type) || field.value.type.enum_def) {
          code += ">";
      }
      code += " {\n";
      code += "        ";
      // Call a different accessor for pointers, that indirects.
      std::string call = std::string() + "self.inner.";
      call += IsScalar(field.value.type.base_type)
        ? "get_field" + (IsEnum(field.value.type) || IsBool(field.value.type)
            // For enums, we need explicit type information on `get_field' (see generated code).
            ? "::<" +  std::string(rstypename[field.value.type.base_type]) + ">"
            : std::string())
        : (IsStruct(field.value.type) ? "get_struct" : "get_ref");
      call += "(" + NumToString(field.value.offset);
      // Default value as second arg for non-pointer types.
      if (IsScalar(field.value.type.base_type))
        call += ", " + field.value.constant;
      call += ")";
      code += GenUnderlyingCast(field, true, call);
      code += "\n";
      code += "    }\n";
      auto nested = field.attributes.Lookup("nested_flatbuffer");
      if (nested) {
        // // TODO: Implement nested flatbuffers.
        // auto nested_root = parser.structs_.Lookup(nested->constant);
        // assert(nested_root);  // Guaranteed to exist by parser.
        // code += "  const " + nested_root->name + " *" + field.name;
        // code += "_nested_root() const { return flatbuffers::GetRoot<";
        // code += nested_root->name + ">(" + field.name + "()->Data()); }\n";
      }
      // Generate a comparison function for this field if it is a key.
      if (field.key) {
        // // TODO: Implement keys.
        // code += "  bool KeyCompareLessThan(const " + struct_def.name;
        // code += " *o) const { return ";
        // if (field.value.type.base_type == BASE_TYPE_STRING) code += "*";
        // code += field.name + "() < ";
        // if (field.value.type.base_type == BASE_TYPE_STRING) code += "*";
        // code += "o->" + field.name + "(); }\n";
        // code += "  int KeyCompareWithValue(";
        // if (field.value.type.base_type == BASE_TYPE_STRING) {
        //   code += "const char *val) const { return strcmp(" + field.name;
        //   code += "()->c_str(), val); }\n";
        // } else {
        //   code += GenTypeBasic(parser, field.value.type, false);
        //   code += " val) const { return " + field.name + "() < val ? -1 : ";
        //   code += field.name + "() > val; }\n";
        // }
      }
    }
  }

  code += "}\n\n";

  // // TODO: Implement Verifiers

  // // Generate a verifier function that can check a buffer from an untrusted
  // // source will never cause reads outside the buffer.
  // code += "  bool Verify(flatbuffers::Verifier &verifier) const {\n";
  // code += "    return VerifyTableStart(verifier)";
  // std::string prefix = " &&\n           ";
  // for (auto it = struct_def.fields.vec.begin();
  //      it != struct_def.fields.vec.end();
  //      ++it) {
  //   auto &field = **it;
  //   if (!field.deprecated) {
  //     code += prefix + "VerifyField";
  //     if (field.required) code += "Required";
  //     code += "<" + GenTypeSize(parser, field.value.type);
  //     code += ">(verifier, " + NumToString(field.value.offset);
  //     code += " /* " + field.name + " */)";
  //     switch (field.value.type.base_type) {
  //       case BASE_TYPE_UNION:
  //         code += prefix + "Verify" + field.value.type.enum_def->name;
  //         code += "(verifier, " + field.name + "(), " + field.name + "_type())";
  //         break;
  //       case BASE_TYPE_STRUCT:
  //         if (!field.value.type.struct_def->fixed) {
  //           code += prefix + "verifier.VerifyTable(" + field.name;
  //           code += "())";
  //         }
  //         break;
  //       case BASE_TYPE_STRING:
  //         code += prefix + "verifier.Verify(" + field.name + "())";
  //         break;
  //       case BASE_TYPE_VECTOR:
  //         code += prefix + "verifier.Verify(" + field.name + "())";
  //         switch (field.value.type.element) {
  //           case BASE_TYPE_STRING: {
  //             code += prefix + "verifier.VerifyVectorOfStrings(" + field.name;
  //             code += "())";
  //             break;
  //           }
  //           case BASE_TYPE_STRUCT: {
  //             if (!field.value.type.struct_def->fixed) {
  //               code += prefix + "verifier.VerifyVectorOfTables(" + field.name;
  //               code += "())";
  //             }
  //             break;
  //           }
  //           default:
  //             break;
  //         }
  //         break;
  //       default:
  //         break;
  //     }
  //   }
  // }
  // code += prefix + "verifier.EndTable()";
  // code += ";\n  }\n";
  // code += "};\n\n";

  // TODO: Implement Builders

  // Generate a builder struct, with methods of the form:
  // void add_name(type name) { fbb_.AddElement<type>(offset, name, default); }
  code += "pub struct " + struct_def.name;
  code += "Builder<'x> {\n";
  code += "    fbb:   &'x mut fb::FlatBufferBuilder,\n";
  code += "    start: fb::UOffset,\n";
  code += "}\n\n";

  code += "impl<'x> " + struct_def.name + "Builder<'x> {\n";
  code += "    pub fn new(fbb: &'x mut fb::FlatBufferBuilder) -> ";
  code += struct_def.name + "Builder<'x> {\n";
  code += "        let start = fbb.start_table();\n";
  code += "        " + struct_def.name + "Builder {\n";
  code += "            fbb:   fbb,\n";
  code += "            start: start,\n";
  code += "        }\n";
  code += "    }\n\n";
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    if (!field.deprecated) {
      code += "    pub fn add_" + field.name + "(&mut self, ";
      if (IsBool(field.value.type)) {
        code += field.name + ": bool";
      } else {
        code += field.name + ": " + GenTypeWire(parser, field.value.type, "", true);
      }
      code += ") {\n";
      code += "        self.fbb.add_";
      if (IsScalar(field.value.type.base_type)) {
        code += "scalar";
        if (IsBool(field.value.type) || IsEnum(field.value.type)) {
          code += "::<" + GenTypeWire(parser, field.value.type, "", false) + ">";
        }
      } else if (IsStruct(field.value.type)) {
        code += "struct";
      } else {
        code += "offset";
      }
      code += "(" + NumToString(field.value.offset) + ", ";
      code += GenUnderlyingCast(field, false, field.name);
      if (IsScalar(field.value.type.base_type))
        code += ", " + field.value.constant;
      code += ")\n    }\n\n";
    }
  }
  code += "    pub fn finish(&mut self) -> fb::Offset<" + struct_def.name;
  code += "> {\n";
  code += "        let o = fb::Offset::new(";
  code += "self.fbb.end_table(self.start, ";
  code += NumToString(struct_def.fields.vec.size()) + "));\n";
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    if (!field.deprecated && field.required) {
      code += "        // self.fbb.required(o, " + NumToString(field.value.offset);
      code += ");  // " + field.name + "\n";
    }
  }
  code += "        o\n    }\n}\n\n";

  // // Generate a convenient CreateX function that uses the above builder
  // // to create a table in one go.
  // code += "inline flatbuffers::Offset<" + struct_def.name + "> Create";
  // code += struct_def.name;
  // code += "(flatbuffers::FlatBufferBuilder &_fbb";
  // for (auto it = struct_def.fields.vec.begin();
  //      it != struct_def.fields.vec.end();
  //      ++it) {
  //   auto &field = **it;
  //   if (!field.deprecated) {
  //     code += ",\n   " + GenTypeWire(parser, field.value.type, " ", true);
  //     code += field.name + " = ";
  //     if (field.value.type.enum_def && IsScalar(field.value.type.base_type)) {
  //       auto ev = field.value.type.enum_def->ReverseLookup(
  //          static_cast<int>(StringToInt(field.value.constant.c_str())), false);
  //       if (ev) {
  //         code += WrapInNameSpace(parser,
  //                                 field.value.type.enum_def->defined_namespace,
  //                                 GenEnumVal(*field.value.type.enum_def, *ev,
  //                                            opts));
  //       } else {
  //         code += GenUnderlyingCast(parser, field, true, field.value.constant);
  //       }
  //     } else {
  //       code += field.value.constant;
  //     }
  //   }
  // }
  // code += ") {\n  " + struct_def.name + "Builder builder_(_fbb);\n";
  // for (size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1;
  //      size;
  //      size /= 2) {
  //   for (auto it = struct_def.fields.vec.rbegin();
  //        it != struct_def.fields.vec.rend();
  //        ++it) {
  //     auto &field = **it;
  //     if (!field.deprecated &&
  //         (!struct_def.sortbysize ||
  //          size == SizeOf(field.value.type.base_type))) {
  //       code += "  builder_.add_" + field.name + "(" + field.name + ");\n";
  //     }
  //   }
  // }
  // code += "  return builder_.Finish();\n}\n\n";
}

static void GenPadding(const FieldDef &field, const std::function<void (int bits)> &f) {
  if (field.padding) {
    for (int i = 0; i < 4; i++)
      if (static_cast<int>(field.padding) & (1 << i))
        f((1 << i) * 8);
    assert(!(field.padding & ~0xF));
  }
}

// Generate an accessor struct with constructor for a flatbuffers struct.
static void GenStruct(const Parser &parser, StructDef &struct_def,
                      std::string *code_ptr) {
  if (struct_def.generated) return;
  std::string &code = *code_ptr;

  // Generate an accessor struct, with private variables of the form:
  // name: type,
  // Generates manual padding and alignment.
  // Variables are private because they contain little endian data on all
  // platforms.
  GenComment(struct_def.doc_comment, code_ptr, nullptr);
  code += "#[derive(Clone,Copy)]\n";
  code += "#[repr(packed)] #[repr(C)] pub struct " + struct_def.name + " {\n";
  int padding_id = 0;
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    code += "    " + field.name + ": ";
    if (IsBool(field.value.type)) {
        code += "u8,\n";
    } else {
        code += GenTypeGet(parser, field.value.type, "", "", "", false) + ",\n";
    }
    GenPadding(field, [&code, &padding_id](int bits) {
      code += "    __padding" + NumToString(padding_id++);
      code += ": u" + NumToString(bits) + ",\n";
    });
  }
  code += "}\n\n";


  // Generate a constructor that takes all fields as arguments.
  code += "impl " + struct_def.name + " {\n";
  code += "    pub fn new(";
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    if (it != struct_def.fields.vec.begin()) code += ", ";
    code += field.name + ": ";
    code += GenTypeGet(parser, field.value.type, "", "&", "", true);
  }
  code += ") -> " + struct_def.name + " {\n";
  padding_id = 0;
  code += "        " + struct_def.name + " {\n";
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    code += "            " + field.name + ": ";
    if (IsScalar(field.value.type.base_type)) {
      code += "fb::Endian::to_le(";
      code += GenUnderlyingCast(field, false, field.name);
      code += "),\n";
    } else {
      code += "*" + field.name + ",\n";
    }
    GenPadding(field, [&code, &padding_id](int bits) {
      (void)bits;
      code += "            __padding" + NumToString(padding_id++) + ": 0,\n";
    });
  }
  code += "        }\n";
  code += "    }\n\n";

  // Generate accessor methods of the form:
  // pub fn name(&self) { fb::Endian::from_le(self.name); }
  for (auto it = struct_def.fields.vec.begin();
       it != struct_def.fields.vec.end();
       ++it) {
    auto &field = **it;
    GenComment(field.doc_comment, code_ptr, nullptr, "  ");
    code += "    pub fn " + field.name + "(&self) -> ";
    code += GenTypeGet(parser, field.value.type, "", "&", "", true);
    code += " { ";
    code += IsScalar(field.value.type.base_type)
        ? GenUnderlyingCast(field, true, "fb::Endian::from_le(self." + field.name + ")")
        : "&self." + field.name;
    code += " }\n\n";
  }
  code += "}\n\n";
}

} // namespace rust

// Iterate through all definitions we haven't generate code for (enums, structs,
// and tables) and output them to a single file.
std::string GenerateRust(const Parser &parser, const GeneratorOptions &opts) {
  using namespace rust;

  // Generate code for all the enum declarations.
  std::string enum_code, enum_code_post;
  for (auto it = parser.enums_.vec.begin();
       it != parser.enums_.vec.end(); ++it) {
    GenEnum(parser, **it, &enum_code, &enum_code_post, opts);
  }

  // Generate code for all structs, then all tables.
  std::string decl_code;
  for (auto it = parser.structs_.vec.begin();
       it != parser.structs_.vec.end(); ++it) {
    if ((**it).fixed) GenStruct(parser, **it, &decl_code);
  }
  for (auto it = parser.structs_.vec.begin();
       it != parser.structs_.vec.end(); ++it) {
    if (!(**it).fixed) GenTable(parser, **it, opts, &decl_code);
  }

  // Only output file-level code if there were any declarations.
  if (enum_code.length() || decl_code.length()) {
    std::string code;
    code = "// automatically generated by the FlatBuffers compiler,"
           " do not modify\n\n";

    code += "use flatbuffers as fb;\n\n";

    if (opts.include_dependence_headers) {
      // No-op for rust.
    }

    // Output the main declaration code from above.
    code += enum_code;
    code += decl_code;
    code += enum_code_post;

    // // Generate convenient global helper functions:
    // if (parser.root_struct_def) {
    //   auto &name = parser.root_struct_def->name;
    //   // The root datatype accessor:
    //   code += "#[inline] pub fn" + name + " *Get";
    //   code += name;
    //   code += "(const void *buf) { return flatbuffers::GetRoot<";
    //   code += name + ">(buf); }\n\n";
    // }

    //   // The root verifier:
    //   code += "inline bool Verify";
    //   code += name;
    //   code += "Buffer(flatbuffers::Verifier &verifier) { "
    //           "return verifier.VerifyBuffer<";
    //   code += name + ">(); }\n\n";

    //   if (parser.file_identifier_.length()) {
    //     // Return the identifier
    //     code += "inline const char *" + name;
    //     code += "Identifier() { return \"" + parser.file_identifier_;
    //     code += "\"; }\n\n";

    //     // Check if a buffer has the identifier.
    //     code += "inline bool " + name;
    //     code += "BufferHasIdentifier(const void *buf) { return flatbuffers::";
    //     code += "BufferHasIdentifier(buf, ";
    //     code += name + "Identifier()); }\n\n";
    //   }

    //   // Finish a buffer with a given root object:
    //   code += "inline void Finish" + name;
    //   code += "Buffer(flatbuffers::FlatBufferBuilder &fbb, flatbuffers::Offset<";
    //   code += name + "> root) { fbb.Finish(root";
    //   if (parser.file_identifier_.length())
    //     code += ", " + name + "Identifier()";
    //   code += "); }\n\n";

    // }

    return code;
  }

  return std::string();
}

static std::string GeneratedFileName(const std::string &path,
                                     const std::string &file_name) {
  return path + file_name + ".rs";
}

bool GenerateRust(const Parser &parser,
                  const std::string &path,
                  const std::string &file_name,
                  const GeneratorOptions &opts) {
    auto code = GenerateRust(parser, opts);
    return !code.length() ||
           SaveFile(GeneratedFileName(path, file_name).c_str(), code, false);
}

}
