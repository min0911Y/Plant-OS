/*
    Name: CodeLogicAst.cpp
    Copyright: Apache 2.0
    Author: CLimber-Rong
    Date: 28/07/23 15:45
    Description: 在这里写代码逻辑（如函数、流程控制等）节点的定义
*/

#pragma once

#include "Ast.hpp"

namespace stamon::ast {
	class AstProgram : public AstNode {

		public:
			AstProgram() : AstNode() {}

			AstProgram(ArrayList<AstNode*>* statements) : AstNode() {
				children = statements;
			}
			virtual int getType() {
				return AstProgramType;
			}
	};
	class AstDefClass : public AstNode {

		public:
			AstDefClass() : AstNode() {}

			AstDefClass(AstIdentifier* iden, AstAnonClass* object_class) : AstNode() {
				children->add((AstNode*)iden);
				children->add((AstNode*)object_class);
			}
			virtual int getType() {
				return AstDefClassType;
			}
	};
	class AstDefFunc : public AstNode {

		public:

			AstDefFunc() : AstNode() {}

			AstDefFunc(AstIdentifier* iden, AstAnonFunc* func) : AstNode() {
				children->add((AstNode*)iden);
				children->add((AstNode*)func);
			}

			virtual int getType() {
				return AstDefFuncType;
			}
	};
	class AstDefVar : public AstNode {

		public:

			AstDefVar() : AstNode() {}

			AstDefVar(AstIdentifier* iden, AstExpression* expr) : AstNode() {
				children->add((AstNode*)iden);
				children->add((AstNode*)expr);
			}

			virtual int getType() {
				return AstDefVarType;
			}
	};
	class AstAnonClass : public AstNode {

		public:
			bool isHaveFather = false;

			AstAnonClass() : AstNode() {}

			AstAnonClass(AstIdentifier* father, ArrayList<AstNode*>* expr) : AstNode() {
				children = expr;
				if(father!=NULL) {
					isHaveFather = true;
					children->insert(0, (AstNode*)father);
				}
			}
			virtual int getType() {
				return AstAnonClassType;
			}

			virtual ~AstAnonClass() = default;
	};
	class AstAnonFunc : public AstNode {

		public:

			AstAnonFunc() : AstNode() {}

			AstAnonFunc(ArrayList<AstNode*>* args, AstBlock* block) : AstNode() {
				children = args;
				children->add((AstNode*)block);
			}
			virtual int getType() {
				return AstAnonFuncType;
			}
	};
	class AstBlock : public AstNode {

		public:

			AstBlock() : AstNode() {}

			AstBlock(ArrayList<AstNode*>* statements) : AstNode() {
				children = statements;
			}
			virtual int getType() {
				return AstBlockType;
			}
	};
	class AstIfStatement : public AstNode {

		public:

			AstIfStatement() : AstNode() {}

			AstIfStatement(AstExpression* expr, AstBlock* block_if) : AstNode() {
				children->add((AstNode*)expr);
				children->add((AstNode*)block_if);
			}
			AstIfStatement(AstExpression* expr, AstBlock* block_if, AstBlock* block_else) : AstNode() {
				children->add((AstNode*)expr);
				children->add((AstNode*)block_if);
				children->add((AstNode*)block_else);
			}
			virtual int getType() {
				return AstIfStatementType;
			}
	};
	class AstWhileStatement : public AstNode {

		public:

			AstWhileStatement() : AstNode() {}

			AstWhileStatement(AstExpression* expr, AstBlock* block_while) : AstNode() {
				children->add((AstNode*)expr);
				children->add((AstNode*)block_while);
			}
			virtual int getType() {
				return AstWhileStatementType;
			}
	};
	class AstForStatement : public AstNode {

		public:

			AstForStatement() : AstNode() {}

			AstForStatement(AstIdentifier* iden, AstExpression* expr, AstBlock* block_for) : AstNode() {
				children->add((AstNode*)iden);
				children->add((AstNode*)expr);
				children->add((AstNode*)block_for);
			}
			virtual int getType() {
				return AstForStatementType;
			}
	};
	class AstReturnStatement : public AstNode {

		public:

			AstReturnStatement() : AstNode() {}

			AstReturnStatement(AstExpression* expr) : AstNode() {
				children->add((AstNode*)expr);
			}
			virtual int getType() {
				return AstReturnStatementType;
			}
	};
}