#define JUST_DEBUG

#include<iostream>

#include"ArrayList.hpp"
#include"NumberMap.hpp"
#include"Stack.hpp"
#include"String.hpp"
#include"StringMap.hpp"
#include"DataType.hpp"
#include"ObjectManager.cpp"
#include"Ast.hpp"
#include"STVCReader.cpp"
#include"Lexer.cpp"

using namespace stamon::ir;
using namespace stamon::datatype;
using namespace stamon::c;
using namespace std;

int main() {
	//在这里编写调试代码，调试方法见文档

	STMException* ex = new STMException();

	Lexer lexer(ex);
	int index;

	index = lexer.getLineTok(1, String((char*)"def a = func { return \"Hello world!\"; }();"));
	CATCH {
		cout<<"ERROR: AT: "<<1<<":"<<index<<" "<<ERROR.getstr()<<endl;
		return 0;
	}

	Token *t;
	while((t=lexer.getTok())->type!=TokenEOF) {
		cout<<"LINE: "<<t->lineNo<<", TYPE: "<<t->type;
		if(t->type==TokenIden) {
			cout<<" IDEN: "<<((IdenToken*)t)->iden.getstr();
		} else if(t->type==TokenInt) {
			cout<<" INT: "<<((IntToken*)t)->val;
		} else if(t->type==TokenDouble) {
			cout<<" DOUBLE: "<<((DoubleToken*)t)->val;
		} else if(t->type==TokenString) {
			cout<<" STRING: "<<((StringToken*)t)->val.getstr();
		}
		cout<<endl;
	}
	return 0;
}