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
#include"Lexer.cpp"
#include"Parser.cpp"

using namespace stamon::ir;
using namespace stamon::datatype;
using namespace stamon::c;
using namespace std;

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

int main() {
	//在这里编写调试代码，调试方法见文档

	STMException* ex = new STMException();	//初始化异常

	freopen("test.ast.txt", "w", stdout);

	LineReader reader(String((char*)"code.st"), ex);	//初始化阅读器

	Lexer lexer(ex);	//初始化词法分析器

	int lineNo = 1;
	while(reader.isMore()) {
		//逐行读取文本并送入lexer分析
		int index = lexer.getLineTok(lineNo, reader.getLine());
		CATCH {
			cout<<"Error: at "<<lineNo<<":"<<index<<" "<<ERROR.getstr()<<endl;
			ex->isError = false;
		}
		lineNo++;
	}

	Matcher matcher(lexer, ex);	//初始化匹配器
	Parser parser(matcher, ex);	//初始化语法分析器

	AstNode* node = parser.Parse();	//开始匹配

	CATCH {
		cout<<"Syntax Error: at "<<parser.ParsingLineNo<<": "<<ERROR.getstr()<<endl;
		return -1;
	}

	DebugAST(node, 0);

	return 0;
}