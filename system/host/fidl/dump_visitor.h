// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ast_visitor.h"

namespace fidl {

class DumpVisitor : public Visitor {
public:
    bool StartVisit(Identifier* identifier) final;
    bool EndVisit(Identifier* identifier) final;
    bool StartVisit(CompoundIdentifier* compound_identifier) final;
    bool EndVisit(CompoundIdentifier* compound_identifier) final;
    // bool StartVisit(Literal* literal) final;
    // bool EndVisit(Literal* literal) final;
    bool StartVisit(StringLiteral* string_literal) final;
    bool EndVisit(StringLiteral* string_literal) final;
    bool StartVisit(NumericLiteral* numeric_literal) final;
    bool EndVisit(NumericLiteral* numeric_literal) final;
    bool StartVisit(TrueLiteral* numeric_literal) final;
    bool EndVisit(TrueLiteral* numeric_literal) final;
    bool StartVisit(FalseLiteral* numeric_literal) final;
    bool EndVisit(FalseLiteral* numeric_literal) final;
    bool StartVisit(DefaultLiteral* default_literal) final;
    bool EndVisit(DefaultLiteral* default_literal) final;
    // bool StartVisit(Type* type) final;
    // bool EndVisit(Type* type) final;
    bool StartVisit(HandleType* handle_type) final;
    bool EndVisit(HandleType* handle_type) final;
    bool StartVisit(IdentifierType* identifier_type) final;
    bool EndVisit(IdentifierType* identifier_type) final;
    bool StartVisit(PrimitiveType* primitive_type) final;
    bool EndVisit(PrimitiveType* primitive_type) final;
    bool StartVisit(RequestType* request_type) final;
    bool EndVisit(RequestType* request_type) final;
    // bool StartVisit(Constant* constant) final;
    // bool EndVisit(Constant* constant) final;
    bool StartVisit(IdentifierConstant* identifier_constant) final;
    bool EndVisit(IdentifierConstant* identifier_constant) final;
    bool StartVisit(LiteralConstant* literal_constant) final;
    bool EndVisit(LiteralConstant* literal_constant) final;
    bool StartVisit(Module* module) final;
    bool EndVisit(Module* module) final;
    bool StartVisit(Using* import) final;
    bool EndVisit(Using* import) final;
    bool StartVisit(UsingList* import_list) final;
    bool EndVisit(UsingList* import_list) final;
    // bool StartVisit(Declaration* declaration) final;
    // bool EndVisit(Declaration* declaration) final;
    bool StartVisit(ConstDeclaration* const_declaration) final;
    bool EndVisit(ConstDeclaration* const_declaration) final;
    // bool StartVisit(EnumMemberValue* enum_field_value) final;
    // bool EndVisit(EnumMemberValue* enum_field_value) final;
    bool StartVisit(EnumMemberValueIdentifier* enum_field_value_identifier) final;
    bool EndVisit(EnumMemberValueIdentifier* enum_field_value_identifier) final;
    bool StartVisit(EnumMemberValueNumeric* enum_field_value_numeric) final;
    bool EndVisit(EnumMemberValueNumeric* enum_field_value_numeric) final;
    bool StartVisit(EnumMember* enum_field) final;
    bool EndVisit(EnumMember* enum_field) final;
    bool StartVisit(EnumBody* enum_body) final;
    bool EndVisit(EnumBody* enum_body) final;
    bool StartVisit(EnumDeclaration* enum_declaration) final;
    bool EndVisit(EnumDeclaration* enum_declaration) final;
    // bool StartVisit(InterfaceMember* interface_field) final;
    // bool EndVisit(InterfaceMember* interface_field) final;
    bool StartVisit(InterfaceMemberConst* interface_field_const) final;
    bool EndVisit(InterfaceMemberConst* interface_field_const) final;
    bool StartVisit(InterfaceMemberEnum* interface_field_enum) final;
    bool EndVisit(InterfaceMemberEnum* interface_field_enum) final;
    bool StartVisit(Parameter* parameter) final;
    bool EndVisit(Parameter* parameter) final;
    bool StartVisit(ParameterList* parameter_list) final;
    bool EndVisit(ParameterList* parameter_list) final;
    bool StartVisit(Response* response) final;
    bool EndVisit(Response* response) final;
    bool StartVisit(InterfaceMemberMethod* interface_field_method) final;
    bool EndVisit(InterfaceMemberMethod* interface_field_method) final;
    bool StartVisit(InterfaceBody* interface_body) final;
    bool EndVisit(InterfaceBody* interface_body) final;
    bool StartVisit(InterfaceDeclaration* interface_declaration) final;
    bool EndVisit(InterfaceDeclaration* interface_declaration) final;
    // bool StartVisit(StructMember* struct_field) final;
    // bool EndVisit(StructMember* struct_field) final;
    bool StartVisit(StructMemberConst* struct_field_const) final;
    bool EndVisit(StructMemberConst* struct_field_const) final;
    bool StartVisit(StructMemberEnum* struct_field_enum) final;
    bool EndVisit(StructMemberEnum* struct_field_enum) final;
    bool StartVisit(StructDefaultValue* struct_default_value) final;
    bool EndVisit(StructDefaultValue* struct_default_value) final;
    bool StartVisit(StructMemberField* struct_field_field) final;
    bool EndVisit(StructMemberField* struct_field_field) final;
    bool StartVisit(StructBody* struct_body) final;
    bool EndVisit(StructBody* struct_body) final;
    bool StartVisit(StructDeclaration* struct_declaration) final;
    bool EndVisit(StructDeclaration* struct_declaration) final;
    bool StartVisit(UnionMember* union_field) final;
    bool EndVisit(UnionMember* union_field) final;
    bool StartVisit(UnionBody* union_body) final;
    bool EndVisit(UnionBody* union_body) final;
    bool StartVisit(UnionDeclaration* union_declaration) final;
    bool EndVisit(UnionDeclaration* union_declaration) final;
    bool StartVisit(DeclarationList* declaration_list) final;
    bool EndVisit(DeclarationList* declaration_list) final;
    bool StartVisit(File* file) final;
    bool EndVisit(File* file) final;

private:
    void Push() { indent_ += 4; }
    void Pop() { indent_ -= 4; }

    void DumpStringView(StringView data);

    int indent_ = 0;
};

} // namespace fidl
