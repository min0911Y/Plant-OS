/*
	Name: ASTBytecode.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 09/02/24 08:48
	Description: ast::Ast-IR生成器
*/

#pragma once

#include"String.hpp"
#include"Stack.hpp"
#include"DataType.hpp"
#include"NumberMap.hpp"
#include"ByteMap.hpp"
#include"StringMap.hpp"
#include"ArrayList.hpp"
#include"Ast.hpp"

//为了方便，我定义了宏
//这些宏只能在本文件中使用

#define CHECK_SPECIAL_AST(type, ast_data) \
	if(top->getType()==type##Type) {\
		rst.data = ((type*)top)->ast_data;\
	}

#define CHECK_IR(ast_type) \
	if(ir[i].type==ast_type##Type) {\
		n = new ast_type();\
		n->lineNo = ir[i].lineNo;\
		n->filename = ir[i].filename;\
	}

#define CHECK_SPECIAL_IR(ast_type, ast_data) \
	if(ir[i].type==ast_type##Type) {\
		n = (ast::AstNode*)new ast_type();\
		n->lineNo = ir[i].lineNo;\
		n->filename = ir[i].filename;\
		((ast_type*)n)->ast_data = ir[i].data;\
	}

namespace stamon::ir {

	class AstIR {
		public:
			int type;
			/*
			 * 一些特别的规定
			 * 当type为-1，代表这是一个结束符，即</>
			 */
			int data;
			//如果这个IR是字面量或标识符，则data存储着其在常量表中的下标
			//否则存储这个IR的具体信息（例如运算符类型）
			String filename;	//IR所在的文件名
			int lineNo;	//IR所在的行号
	};

	enum {
	    IdenConstTypeID = -1
	};

	class IdenConstType : public datatype::DataType {
			//我需要在生成常量表时将标识符和数据类型统一
			//所以我建立了IdenConstType
			String iden;
		public:
			IdenConstType(const String& value) : datatype::DataType(-1) {
				iden = value;
			}
			virtual String getVal() const {
				return iden;
			}
	};

	class AstLeaf : public ast::AstNode {
			int index;	//常量表下标
		public:
			AstLeaf(int data) : AstNode() {
				index = data;
			}
			virtual int getVal() const {
				return index;
			}
			virtual int getType() {
				return ast::AstLeafType;
			}
	};

	class AstIRGenerator {
		public:

			ArrayList<datatype::DataType*> tableConst;
			ByteMap<void> tableConstFloat;
			ByteMap<void> tableConstDouble;
			NumberMap<void> tableConstInt;
			StringMap<void> tableConstString;
			StringMap<void> tableConstIden;
			/*
			 * 这里需要解释一下
			 * 以上是不同类型的常量表
			 * 无论是ByteMap，抑或是NumberMap、StringMap
			 * 如果设置模板类型为void，即代表该map的值的类型为void*（详见库的定义）
			 * 这些void*里存的是整数，即键在tableConst当中的下标
			 * 举个例子：
				 * 遇到整数常量1145
				 * 以1145为键，在tableConstInt中查找值
				 * 假设查到值为14
				 * 则代表tableConst[14]是一个IntegerType，且值为1145
			 */

			int createConst(datatype::DataType* dt) {
				/*
				 * 新建一个常量，常量的值为dt
				 * 在调用这个函数前，请确保dt不在常量表中
				 * 返回新建后，该常量在常量表中的下标
				 */
				tableConst.add(dt);

				int index = tableConst.size()-1;

				if(dt->getType()==datatype::IntegerTypeID) {
					datatype::IntegerType* dtInt = (datatype::IntegerType*)dt;
					tableConstInt.put(dtInt->getVal(), *(void**)&index);
					//*(void**)&index等价于(void*)index
				}

				if(dt->getType()==datatype::FloatTypeID) {
					datatype::FloatType* dtFlt = (datatype::FloatType*)dt;

					float val = dtFlt->getVal();
					char* key = (char*)&val;

					tableConstFloat.put(
					    key, sizeof(float), *(void**)&index
					);
				}

				if(dt->getType()==datatype::DoubleTypeID) {
					datatype::DoubleType* dtDb = (datatype::DoubleType*)dt;

					double val = dtDb->getVal();
					char* key = (char*)&val;

					tableConstFloat.put(
					    key, sizeof(double), *(void**)&index
					);
				}

				if(dt->getType()==datatype::StringTypeID) {
					datatype::StringType* dtStr = (datatype::StringType*)dt;

					String key = dtStr->getVal();

					tableConstString.put(key, *(void**)&index);
				}

				if(dt->getType()==-1) {
					//新建的是标识符
					IdenConstType* dtIden = (IdenConstType*)dt;

					String key = dtIden->getVal();

					tableConstIden.put(
					    key, *(void**)&index
					);
				}

				return index;
			}

			int searchConst(datatype::DataType* dt) {
				//查找dt在常量表当中的下标
				//如果dt不在常量表中，则返回-1

				if(dt->getType()==datatype::IntegerTypeID) {
					datatype::IntegerType* dtInt = (datatype::IntegerType*)dt;
					if(!tableConstInt.containsKey(dtInt->getVal())) {
						//不在常量表中
						return -1;
					}
					return (long long)tableConstInt.get(dtInt->getVal());
				}

				if(dt->getType()==datatype::FloatTypeID) {
					datatype::FloatType* dtFlt = (datatype::FloatType*)dt;

					float val = dtFlt->getVal();
					char* key = (char*)&val;

					if(!tableConstFloat.containsKey(key, sizeof(float))) {
						return -1;
					}
					return (long long)tableConstFloat.get(
					           key, sizeof(float)
					       );
				}

				if(dt->getType()==datatype::DoubleTypeID) {
					datatype::DoubleType* dtDb = (datatype::DoubleType*)dt;

					double val = dtDb->getVal();
					char* key = (char*)&val;

					if(!tableConstDouble.containsKey(key, sizeof(double))) {
						return -1;
					}
					return (long long)tableConstDouble.get(
					           key, sizeof(double)
					       );
				}

				if(dt->getType()==datatype::StringTypeID) {
					datatype::StringType* dtStr = (datatype::StringType*)dt;

					String key = dtStr->getVal();

					if(!tableConstString.containsKey(key)) {
						return -1;
					}
					return (long long)tableConstString.get(key);
				}

				if(dt->getType()==-1) {
					//查找的是标识符
					IdenConstType* dtIden = (IdenConstType*)dt;

					String key = dtIden->getVal();

					if(!tableConstIden.containsKey(key)) {
						return -1;
					}
					return (long long)tableConstIden.get(key);
				}

				return -1;
			}

			ArrayList<AstIR> gen(ast::AstNode* program) {

				Stack<ast::AstNode> stack;
				ArrayList<AstIR> list;

				if(program==NULL) {
					stack.destroy();
					return list;
				}

				//先把__init__放入常量表
				//这也就意味着__init__始终在常量表的第一位
				createConst(new IdenConstType(String((char*)"__init__")));

				stack.push(program);

				//迭代遍历语法树，编译成AstIR

				while(stack.empty()==false) {
					bool isLeafNode = false;
					AstIR rst;
					ast::AstNode* top = stack.pop();	//弹出栈顶

					rst.type = top->getType();
					rst.data = 0;	//默认为0
					rst.lineNo = top->lineNo;
					rst.filename = top->filename;


					//先特判一些节点
					CHECK_SPECIAL_AST(ast::AstAnonClass, isHaveFather)
					CHECK_SPECIAL_AST(ast::AstExpression, ass_type)
					CHECK_SPECIAL_AST(ast::AstLeftPostfix, getPostfixType())
					CHECK_SPECIAL_AST(ast::AstBinary, getOperatorType())
					CHECK_SPECIAL_AST(ast::AstUnary, getOperatorType())
					CHECK_SPECIAL_AST(ast::AstPostfix, getPostfixType())
					//特判结束符
					if(top->getType()==ast::AstNodeType) {
						isLeafNode = true;
						rst.type = -1;
						rst.lineNo = -1;
						rst.filename = String((char*)"");
					}
					//特判叶子节点

					if(
					    top->getType() == ast::AstNumberType
					    ||top->getType()==ast::AstStringType
					    ||top->getType()==ast::AstIdentifierType
					) {
						isLeafNode = true;
						rst.type = ast::AstLeafType;	//统一代表叶子节点
					}

					if(top->getType()==ast::AstIdentifierType) {

						String iden = ((ast::AstIdentifierName*)top)->getName();
						IdenConstType* dtIden = new IdenConstType(iden);

						int index;	//常量表下标

						index = searchConst(dtIden);

						if(index==-1) {
							index = createConst(dtIden);
						}

						rst.data = index;
					}

					if(top->getType()==ast::AstStringType) {

						String s = ((ast::AstString*)top)->getVal();
						datatype::StringType* dt = new datatype::StringType(s);

						int index;	//常量表下标

						index = searchConst(dt);

						if(index==-1) {
							index = createConst(dt);
						}

						rst.data = index;
					}

					if(top->getType()==ast::AstNumberType) {

						int num_type = ((ast::AstNumber*)top)->getNumberType();
						if(num_type==ast::IntNumberType) {
							int n = ((ast::AstIntNumber*)top)->getVal();
							datatype::IntegerType* dt
							    = new datatype::IntegerType(n);

							int index;	//常量表下标

							index = searchConst(dt);

							if(index==-1) {
								index = createConst(dt);
							}

							rst.data = index;
						}
						if(num_type==ast::FloatNumberType) {
							int n = ((ast::AstFloatNumber*)top)->getVal();
							datatype::FloatType* dt
							    = new datatype::FloatType(n);

							int index;	//常量表下标

							index = searchConst(dt);

							if(index==-1) {
								index = createConst(dt);
							}

							rst.data = index;
						}
						if(num_type==ast::DoubleNumberType) {
							int n = ((ast::AstDoubleNumber*)top)->getVal();
							datatype::DoubleType* dt
							    = new datatype::DoubleType(n);

							int index;	//常量表下标

							index = searchConst(dt);

							if(index==-1) {
								index = createConst(dt);
							}

							rst.data = index;
						}
					}

					//将AST IR存入列表

					list.add(rst);

					if(isLeafNode==false) {
						//压入终结符
						stack.push(new ast::AstNode(rst.lineNo));

						//接着遍历子节点
						for(int i=top->Children()->size()-1; i>=0; i--) {
							stack.push(top->Children()->at(i));
						}

					}

				}

				stack.destroy();

				return list;
			}

			ast::AstNode* read(ArrayList<AstIR> ir) {
				/*
				 * AstIR转ast::Ast
				 * 如果ir不正确，程序会运行时错误
				 * 所以请在运行该函数前检查ir
				 */

				Stack<ast::AstNode> stack;

				ast::AstNode* rst;

				for(int i=0,len=ir.size(); i<len; i++) {
					if(ir[i].type==-1) {
						//结束符
						stack.pop();
						continue;
					}
					if(ir[i].type==ast::AstLeafType) {
						//叶子节点
						AstLeaf* leaf = new AstLeaf(ir[i].data);
						leaf->lineNo = ir[i].lineNo;
						leaf->filename = ir[i].filename;

						stack.peek()->Children()->add(
						    leaf
						);
						continue;
					}
					ast::AstNode* n = NULL;
					CHECK_IR(ast::AstProgram)
					CHECK_IR(ast::AstDefClass)
					CHECK_IR(ast::AstDefFunc)
					CHECK_IR(ast::AstDefVar)
					CHECK_IR(ast::AstAnonFunc)
					CHECK_IR(ast::AstBlock)
					CHECK_IR(ast::AstBreak)
					CHECK_IR(ast::AstContinue)
					CHECK_IR(ast::AstIfStatement)
					CHECK_IR(ast::AstWhileStatement)
					CHECK_IR(ast::AstForStatement)
					CHECK_IR(ast::AstReturnStatement)
					CHECK_IR(ast::AstSFN)
					CHECK_IR(ast::AstLeftValue)
					CHECK_IR(ast::AstArguments)
					CHECK_IR(ast::AstNull)
					CHECK_IR(ast::AstArrayLiteral)
					CHECK_IR(ast::AstListLiteral)

					CHECK_SPECIAL_IR(ast::AstAnonClass, isHaveFather)
					CHECK_SPECIAL_IR(ast::AstExpression, ass_type)
					CHECK_SPECIAL_IR(ast::AstLeftPostfix, postfix_type)
					CHECK_SPECIAL_IR(ast::AstBinary, operator_type)
					CHECK_SPECIAL_IR(ast::AstUnary, operator_type)
					CHECK_SPECIAL_IR(ast::AstPostfix, postfix_type)

					if(stack.empty()==false) {
						//说明有父节点，那么绑定它
						stack.peek()->Children()->add(n);
					} else {
						rst = n;
						//说明这个节点是总节点
					}
					//压入
					stack.push(n);
				}

				stack.destroy();

				return rst;
			}
	};
} //namespace stamon::ir


#undef CHECK_SPECIAL_AST