/*
	Name: SfnAst.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 01/08/23 19:55
	Description: SFN节点的基本定义
*/

#pragma once

#include"Ast.hpp"
#include"String.hpp"

namespace stamon::ast {
	class AstSFN : public AstNode {
		public:
			AstSFN() : AstNode() {}
			AstSFN(AstIdentifier* port, AstIdentifier* result) : AstNode() {
				children->add(port);
				children->add(result);
			}
			virtual int getType() {
				return AstSFNType;
			}
	};
}