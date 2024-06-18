/*
	Name: test.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 09/09/23 13:54
	Description: 测试用
*/

#define JUST_DEBUG

#include<iostream>

#include"ArrayList.hpp"
#include"NumberMap.hpp"
#include"Stack.hpp"
#include"String.hpp"
#include"LineReader.hpp"
#include"StringMap.hpp"
#include"DataType.hpp"
#include"ObjectManager.cpp"
#include"Ast.hpp"
#include"STVCReader.cpp"
#include"Preprocessor.cpp"

using namespace stamon::ir;
using namespace stamon::datatype;
using namespace stamon::c;
using namespace std;

void DebugAST(AstNode* node, int layer);

int main() {
	//在这里编写调试代码，调试方法见文档

	STMException* ex = new STMException();

	Preprocessor* prr = new Preprocessor(ex);

	ArrayList<SourceSyntax> rst = prr->ParseSource(String((char*)"code.st"), true);

	if(prr->ErrorMsg.empty()==false) {
		//有报错信息
		cout<<"[Compile Error]"<<endl<<endl;
		for(int i=0,len=prr->ErrorMsg.size();i<len;i++) {
			cout<<prr->ErrorMsg[i].getstr()<<endl;
		}
		return -1;
	}

	for(int i=0,len=rst.size();i<len;i++) {
		cout<<"@FILE: "<<rst[i].filename.getstr()<<endl;
		DebugAST(rst[i].program, 1);
	}

	return 0;
}

void DebugAST(AstNode* node, int layer) {
	for(int i=1; i<=layer; i++) cout<<'\t';
	cout<<"TYPE: "<<node->getType();

	//特判表达式
	if(node->getType()==AstExpressionType) {
		AstExpression* rst = (AstExpression*)node;
		cout<<" EXPRESSION: "<<rst->ass_type;
	}

	//特判left_postfix
	if(node->getType()==AstLeftPostfixType) {
		AstLeftPostfix* rst = (AstLeftPostfix*)node;
		cout<<" LEFT-POSTFIX: "<<rst->getPostfixType();
	}

	//特判postfix
	if(node->getType()==AstPostfixType) {
		AstPostfix* rst = (AstPostfix*)node;
		cout<<" POSTFIX: "<<rst->getPostfixType();
	}

	//特判单目运算符
	if(node->getType()==AstUnaryType) {
		AstUnary* rst = (AstUnary*)node;
		cout<<" UNARY: "<<rst->getOperatorType();
	}
	//特判双目运算符
	if(node->getType()==AstBinaryType) {
		AstBinary* rst = (AstBinary*)node;
		cout<<" BINARY: "<<rst->getOperatorType();
	}

	//ast的叶子节点要特判
	if(node->getType()==AstIdentifierType) {
		AstIdentifierName* rst = (AstIdentifierName*)node;
		cout<<" IDEN: "<<rst->getName().getstr();
	}
	if(node->getType()==AstNumberType) {
		AstNumber* rst = (AstNumber*)node;
		if(rst->getNumberType()==IntNumberType) {
			cout<<" INT: "<<((AstIntNumber*)rst)->getVal();
		}
		if(rst->getNumberType()==DoubleNumberType) {
			cout<<" DOUBLE: "<<((AstDoubleNumber*)rst)->getVal();
		}
	}
	if(node->getType()==AstStringType) {
		AstString* rst = (AstString*)node;
		cout<<" STRING: "<<rst->getVal().getstr();
	}
	cout<<endl;
	for(int i=0,len=node->Children()->size(); i<len; i++) {
		DebugAST(node->Children()->at(i), layer+1);
	}
	return;
}