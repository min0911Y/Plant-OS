/*
	Name: test.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 09/09/23 13:54
	Description: 测试用
*/

#define JUST_DEBUG

#include<stdio.h>

#include"Stamon.hpp"

using namespace stamon;
using namespace stamon::ast;
using namespace stamon::c;
using namespace stamon::datatype;
using namespace stamon::ir;
using namespace stamon::sfn;
using namespace std;

void DebugAST(AstNode* node, int layer);
void DebugIR(ArrayList<AstIR> ir);

int main() {
	//在这里编写调试代码，调试方法见文档

	return 0;

}

//一些用于调试的函数

void DebugAST(AstNode* node, int layer) {
	for(int i=1; i<=layer; i++) printf("\t");
	printf("TYPE: %d", node->getType());

	//特判表达式
	if(node->getType()==AstExpressionType) {
		AstExpression* rst = (AstExpression*)node;
		printf(" EXPRESSION: %d", rst->ass_type);
	}

	//特判left_postfix
	if(node->getType()==AstLeftPostfixType) {
		AstLeftPostfix* rst = (AstLeftPostfix*)node;
		printf(" LEFT-POSTFIX: %d", rst->getPostfixType());
	}

	//特判postfix
	if(node->getType()==AstPostfixType) {
		AstPostfix* rst = (AstPostfix*)node;
		printf(" POSTFIX:  %d",rst->getPostfixType());
	}

	//特判单目运算符
	if(node->getType()==AstUnaryType) {
		AstUnary* rst = (AstUnary*)node;
		printf(" UNARY:  %d",rst->getOperatorType());
	}
	//特判双目运算符
	if(node->getType()==AstBinaryType) {
		AstBinary* rst = (AstBinary*)node;
		printf(" BINARY:  %d",rst->getOperatorType());
	}

	//ast的叶子节点要特判
	if(node->getType()==AstIdentifierType) {
		AstIdentifierName* rst = (AstIdentifierName*)node;
		printf(" IDEN:  %s",rst->getName().getstr());
	}
	if(node->getType()==AstNumberType) {
		AstNumber* rst = (AstNumber*)node;
		if(rst->getNumberType()==IntNumberType) {
			printf(" INT:  %d",((AstIntNumber*)rst)->getVal());
		}
		if(rst->getNumberType()==DoubleNumberType) {
			printf(" DOUBLE:  %lf",((AstDoubleNumber*)rst)->getVal());
		}
	}
	if(node->getType()==AstStringType) {
		AstString* rst = (AstString*)node;
		printf(" STRING:  %s",rst->getVal().getstr());
	}
	printf("\n");
	for(int i=0,len=node->Children()->size(); i<len; i++) {
		DebugAST(node->Children()->at(i), layer+1);
	}
	return;
}

void DebugIR(ArrayList<AstIR> ir) {
	for(int i=0,len=ir.size(); i<len; i++) {
		printf("TYPE: %d\n", ir[i].type);
	}
}