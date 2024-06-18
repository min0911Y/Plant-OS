/*
	Name: AstRunner.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 11/02/24 14:16
	Description: 语法树的运行器
*/

#ifndef ASTRUNNER_CPP
#define ASTRUNNER_CPP


#include"ArrayList.hpp"
#include"Exception.hpp"
#include"String.hpp"
#include"stmlib.hpp"
#include"Ast.hpp"
#include"DataType.hpp"
#include"ObjectManager.cpp"
#include"AstIR.cpp"
#include"SFN.cpp"
#include"Parser.cpp"

int TMP = 0;

#define CDT(dt, type) \
	if(dt->getType()!=type##ID) {\
		ThrowTypeError(type##ID);\
	}\
	CE

//强制检查数据类型（Check Data Type）

#define OPND_PUSH(data)	manager->OPND.add(data);
#define OPND_POP manager->OPND.erase(manager->OPND.size()-1);
#define RUN(ast) runAst(ast); CE
#define GETDT(var, code) \
	if(true) {\
		RetStatus tmp = (code);\
		CE\
		var = tmp.retval->data;\
	}
//Get DataType，想要安全的获取DataType，应该使用这个宏


#define CE			CATCH { return RetStatus(RetStatusErr, NULL); }
//如果执行代码中出现异常，直接返回
#define CTH(message)	CATCH { THROW(message) }
//如果执行代码中出现异常，抛出异常
#define CTH_S(message)	CATCH { THROW_S(message) }

#define OPERATE_BINARY(type, op) \
	if(bin_node->getOperatorType()==Binary##type##Type) {\
		RetStatus left_st = RUN(bin_node->Children()->at(0));\
		DataType* left = left_st.retval->data;\
		CDT(left, IntegerType)\
		OPND_PUSH(left)\
		\
		RetStatus right_st = RUN(bin_node->Children()->at(1));\
		DataType* right = right_st.retval->data;\
		CDT(left, IntegerType)\
		OPND_PUSH(right)\
		\
		DataType* rst = manager->MallocObject<IntegerType>(\
		                ((IntegerType*)left)->getVal()\
		                op ((IntegerType*)right)->getVal()\
		                                                  );\
		CE\
		\
		OPND_POP\
		OPND_POP\
		\
		return RetStatus(RetStatusNor, new Variable(rst));\
	}

//这个宏用于简便编写，并且只能用于整数之间的运算

#define ASMD_OPERATE(op, ErrCheck) \
	if(t1->getType()==IntegerTypeID) {\
		auto l = (IntegerType*)t1;\
		auto r = (IntegerType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<IntegerType>(\
		        l->getVal() op r->getVal()\
		                                        );\
	} else if(t1->getType()==FloatTypeID) {\
		auto l = (FloatType*)t1;\
		auto r = (FloatType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<FloatType>(\
		                                       l->getVal() op r->getVal()\
		                                      );\
	} else if(t1->getType()==DoubleTypeID) {\
		auto l = (DoubleType*)t1;\
		auto r = (DoubleType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<DoubleType>(\
		                                        l->getVal() op r->getVal()\
		                                       );\
	}\
	CE

//专门用于加减乘除运算的宏（Add Sub Mul Div）
//其中的ErrCheck用于除法中，检测除数是否为0

//利用t1、t2拷贝了一份left和right，使其原值不受影响

#define DIV_ERRCHECK \
	if(r->getType()==0) {\
		ThrowDivZeroError();\
	}
//用于ASMD_OPERATE中的ErrCheck

#define MATH_OPERATE(type, op, ErrCheck) \
	if(bin_node->getOperatorType()==Binary##type##Type) {\
		RetStatus left_st = RUN(bin_node->Children()->at(0));\
		DataType* left = left_st.retval->data;\
		OPND_PUSH(left)\
		\
		RetStatus right_st = RUN(bin_node->Children()->at(1));\
		DataType* right = right_st.retval->data;\
		OPND_PUSH(right)\
		\
		DataType* t1;\
		DataType* t2;\
		BinaryOperatorConvert(left, t1, right, t2);\
		CE\
		\
		DataType* rst;\
		\
		ASMD_OPERATE(op, ErrCheck)\
		\
		OPND_POP\
		OPND_POP\
		\
		return RetStatus(RetStatusNor, new Variable(rst));\
	}

#define BIND(name) RunAstFunc[Ast##name##Type] = &AstRunner::run##name;

#define CHECK_ASS(op_type, op, ErrCheck) \
	if(expr_node->ass_type==Token##op_type##Ass) {\
		DataType* t1;\
		DataType* t2;\
		BinaryOperatorConvert(left_value->data, t1, right_value->data, t2);\
		OPND_PUSH(t1);\
		OPND_PUSH(t2);\
		if(t1->getType()==IntegerTypeID) {\
			auto l = (IntegerType*)t1;\
			auto r = (IntegerType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<IntegerType>(\
			                   l->getVal() op r->getVal()\
			                                                     );\
		} else if(t1->getType()==FloatTypeID) {\
			auto l = (FloatType*)t1;\
			auto r = (FloatType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<FloatType>(\
			                   l->getVal() op r->getVal()\
			                                                   );\
		} else if(t1->getType()==DoubleTypeID) {\
			auto l = (DoubleType*)t1;\
			auto r = (DoubleType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<DoubleType>(\
			                   l->getVal() op r->getVal()\
			                                                    );\
		}\
		CE\
		OPND_POP\
		OPND_POP\
	}


#define CHECK_INT_ASS(op_type, op) \
	if(expr_node->ass_type==Token##op_type##Ass) {\
		CDT(left_value->data, IntegerType)\
		CDT(right_value->data, IntegerType)\
		auto t1 = manager->MallocObject<IntegerType>(\
		          ((IntegerType*)left_value->data)->getVal()\
		                                            );\
		OPND_PUSH(t1)\
		auto t2 = manager->MallocObject<IntegerType>(\
		          ((IntegerType*)right_value->data)->getVal()\
		                                            );\
		OPND_PUSH(t2)\
		left_value->data = manager\
		                   ->MallocObject<IntegerType>(\
		                           t1->getVal() op t2->getVal()\
		                                              );\
		OPND_POP\
		OPND_POP\
		CE\
	}

namespace stamon {
	namespace vm {
		using namespace ir;
		using namespace c;
		using namespace ast;
		using namespace datatype;
		using namespace sfn;



		enum RET_STATUS_CODE {	//返回的状态码集合
		    RetStatusErr = -1,	//错误退出（Error）
		    RetStatusNor,		//正常退出（Normal）
		    RetStatusCon,		//继续循环（Continue）
		    RetStatusBrk,		//退出循环（Break）
		    RetStatusRet		//函数返回（Return）
		};

		class RetStatus {	//返回的状态（Return Status）
				//这个类用于运行时
			public:
				int status;	//状态码
				Variable* retval;	//返回值（Return-Value），无返回值时为NULL
				RetStatus() {}
				RetStatus(const RetStatus& right) {
					status = right.status;
					retval = right.retval;
				}
				RetStatus(int status_code, Variable* retvalue) {
					status = status_code;
					retval = retvalue;
				}
		};

		class AstRunner {

			public:
				ObjectManager* manager;
				RetStatus(AstRunner::*RunAstFunc[AstTypeNum])(AstNode* node);
				//由类成员函数指针组成的数组
				AstNode* program;
				bool is_gc;	//是否允许gc
				int gc_mem_limit;	//对象内存最大限制
				ArrayList<DataType*> tabconst;	//常量表
				ArrayList<String> vm_args;	//虚拟机参数
				STMException* ex;	//异常
				SFN sfn;

				int RunningLineNo;
				String RunningFileName;

				AstRunner() {
					BIND(Program)
					BIND(DefClass)
					BIND(DefFunc)
					BIND(DefVar)
					BIND(AnonClass)
					BIND(AnonFunc)
					BIND(Block)
					BIND(Break)
					BIND(Continue)
					BIND(IfStatement)
					BIND(WhileStatement)
					BIND(ForStatement)
					BIND(ReturnStatement)
					BIND(SFN)
					BIND(Expression)
					BIND(LeftValue)
					BIND(Binary)
					BIND(Unary)
					BIND(Leaf)
					BIND(Null)
					BIND(ArrayLiteral)
					BIND(ListLiteral)

				}

				/**
				 * \brief 执行程序
				 *
				 * \param main_node 虚拟机的入口ast节点，即AstProgram
				 * \param isGC 是否允许gc
				 * \param vm_mem_limit 虚拟机内存的最大限制
				 * \param tableConst 常量表
				 * \param args 虚拟机的命令行参数
				 * \param e 异常对象，虚拟机发生异常时会将异常信息存入
				 *
				 * \return 程序的执行状态
				 */

				String getDataTypeName(int type);
				void ThrowTypeError(int type);
				void ThrowPostfixError();
				void ThrowIndexError();
				void ThrowConstantsError();
				void ThrowDivZeroError();
				void ThrowBreakError();
				void ThrowContinueError();
				void ThrowArgumentsError(int form_args, int actual_args);
				void ThrowReturnError();
				void ThrowUnknownOperatorError();
				void ThrowUnknownMemberError(int id);

				RetStatus excute(
				    AstNode* main_node, bool isGC, int vm_mem_limit,
				    ArrayList<DataType*> tableConst, ArrayList<String> args,
				    STMException* e
				) {

					//初始化参数
					program = main_node;
					is_gc = isGC;
					gc_mem_limit = vm_mem_limit;
					tabconst = tableConst;
					vm_args = args;
					
					ex = e;
					//初始化对象管理器
					manager = new ObjectManager(is_gc,
					                            vm_mem_limit, ex);

					sfn = SFN(e, manager);

					//执行程序
					auto st = runAst(program);
					//释放内存

					delete manager;

					return st;
				}

				RetStatus runAst(AstNode* node) {
					RunningLineNo = node->lineNo;
					RunningFileName = node->filename;

					if(node->getType()==-1) {
						//叶子节点
						return runLeaf(node);
					} else {
						return (this->*(RunAstFunc[node->getType()]))(node);
					}
				}

				RetStatus runProgram(AstNode* node) {
					manager->PushScope();	//全局作用域

					for(int i=0,len=node->Children()->size(); i<len; i++) {
						auto st = RUN(node->Children()->at(i));
						if(st.status!=RetStatusNor) {
							if(st.status==RetStatusRet) {
								ThrowReturnError();
							} else if(st.status==RetStatusBrk) {
								ThrowBreakError();
							} else if(st.status==RetStatusCon) {
								ThrowContinueError();
							}
							CE
						}

					}

					manager->PopScope();

					return RetStatus(RetStatusNor, NULL);
				}

				ObjectType* initObject(ClassType* cls) {
					ObjectType* rst;
					NumberMap<Variable> membertab;	//成员表
					auto node = cls->getVal();

					/*处理父类*/
					if(node->isHaveFather == true) {
						//有父类，先初始化父类
						auto father_class = manager->GetVariable(
						                        (
						                            (AstIdentifier*)
						                            node
						                            ->Children()
						                            ->at(0)
						                        )
						                        ->getID()
						                    );

						if(father_class->data->getType()!=ClassTypeID) {
							ThrowTypeError(father_class->data->getType());
							return NULL;
						}

						rst = initObject((ClassType*)father_class);
						CATCH {
							return NULL;
						}

						membertab.destroy();	//先销毁之前创建的空表

						membertab = rst->getVal();

					} else {
						rst = manager->MallocObject<ObjectType>(membertab);

						CATCH {
							return NULL;
						}
					}

					OPND_PUSH(rst);

					/*接着继续初始化*/
					manager->PushScope(ObjectScope(membertab));
					//将membertab注入到新的作用域当中
					//这样所有的操作都会直接通过membertab反馈到rst

					for(int i=0,len=node->Children()->size(); i<len; i++) {
						if(i==0 && node->isHaveFather) {
							//如果有父类，则要从第二个节点开始
							continue;
						}

						auto n = node->Children()->at(i); //用n代替这个复杂的表达式

						if(n->getType()==AstDefClassType) {
							runDefClass(n);
						} else if(n->getType()==AstDefFuncType) {
							runDefMethod(rst, n);
						} else if(n->getType()==AstDefVarType) {
							runDefVar(n);
						}

						CATCH {
							return NULL;
						}

					}

					/*收尾*/
					OPND_POP	//弹出rst
					manager->PopScope();

					return rst;
				}

				RetStatus runMethod(
				    MethodType* method, ArrayList<AstNode*>* args
				) {
					OPND_PUSH(method);

					/*返回的结果，默认返回null*/
					DataType* rst = manager->MallocObject<NullType>();
					CE

					OPND_PUSH(rst);

					/*先获取容器*/
					auto container = method->getContainer();

					/*再获得形参*/
					auto FormArg = method->getVal()->Children()->clone();
					FormArg.erase(FormArg.size()-1);	//删除AstBlock，只保留参数

					/*接着计算实际参数的表达式*/
					ArrayList<DataType*> args_val;

					if(container!=NULL) {
						//说明该函数有容器，即该函数是方法
						//规定方法的第一个参数为容器
						args_val.add(container);
						OPND_PUSH(container)
					}

					for(int i=0,len=args->size(); i<len; i++) {

						auto st = RUN(args->at(i));	//计算参数

						args_val.add(st.retval->data);	//加入参数表

						OPND_PUSH(args_val[args_val.size()-1]);	//压入参数
					}

					for(int i=0,len=args_val.size(); i<len; i++) {
						OPND_POP	//弹出参数
					}

					/*然后对比形参和实参的个数*/
					if(FormArg.size()!=args_val.size()) {
						//形式参数个数要等于实际参数个数
						ThrowArgumentsError(FormArg.size(), args_val.size());
						CE
					}

					/*新建作用域*/
					manager->PushScope();

					/*如果有函数名，就存入它*/
					if(method->id!=-1) {
						manager->NewVariable(method->id, method);
					}



					/*存入参数*/
					for(int i=0,len=FormArg.size(); i<len; i++) {
						manager->NewVariable(
						    (
						        (AstIdentifier*)FormArg.at(i)
						    )
						    ->getID(),
						    args_val[i]
						);
					}

					/*执行函数体*/
					auto st = RUN(
					              method
					              ->getVal()
					              ->Children()
					              ->at(FormArg.size())
					          );
					/*
					 * 这里用到了一些简便思路
					 * 我想要运行函数体（即AstBlock）
					 * 函数体位于Children()的最后一个元素
					 * 如果直接暴力的访问最后一个元素，那么代码要写为
					 * method->getVal()->Children()->at(
					 * 		method->getVal()->Children->size()-1
					 * )
					 * 这样太复杂了
					 * 我注意到：
					 * 		FormArg.size()
					 * 		= method->getVal()->Children->size()-1
					 * 所以以FormArg.size()访问函数体
					 */

					/*弹出作用域*/
					manager->PopScope();
					OPND_POP	//弹出rst
					OPND_POP	//弹出method



					/*返回*/
					if(st.status==RetStatusRet) {
						//有返回值
						return RetStatus(RetStatusNor, st.retval);
					}
					//无返回值，返回rst（即null）
					return RetStatus(RetStatusNor, new Variable(rst));
				}

				RetStatus runBlock(AstNode* node) {
					auto blk_node = (AstBlock*)node;
					ArrayList<AstNode*>* codes = node->Children();

					for(int i=0,len=codes->size(); i<len; i++) {
						auto st = RUN(codes->at(i));
						if(
						    st.status==RetStatusBrk
						    || st.status==RetStatusCon
						    || st.status==RetStatusRet
						) {
							return st;
						}
					}

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runDefVar(AstNode* node) {
					auto val = RUN(node->Children()->at(1));
					manager->NewVariable(
					    (
					        (AstIdentifier*)node->Children()->at(0)
					    )
					    ->getID(),
					    val.retval->data
					);
					return RetStatus(RetStatusNor, NULL);
				}

				MethodType* runDefMethod(ObjectType* container, AstNode* node) {

					auto iden = (
					                (AstIdentifier*)node->Children()->at(0)
					            )
					            ->getID();

					RetStatus st = runAst(node->Children()->at(1));

					CATCH {
						return NULL;
					}

					auto func = (MethodType*)st.retval->data;

					func->id = iden;
					func->container = container;

					manager->NewVariable(iden, func);

					return func;
				}

				RetStatus runDefFunc(AstNode* node) {
					auto iden = (
					                (AstIdentifier*)node->Children()->at(0)
					            )
					            ->getID();

					auto st = RUN(node->Children()->at(1));

					auto func = (MethodType*)st.retval->data;

					func->id = iden;

					manager->NewVariable(iden, func);

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runDefClass(AstNode* node) {
					auto iden = (
					                (AstIdentifier*)node->Children()->at(0)
					            )
					            ->getID();
					auto st = RUN(node->Children()->at(1));

					auto cls = (ClassType*)st.retval->data;

					manager->NewVariable(iden, cls);

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runAnonClass(AstNode* node) {
					auto ancl_node = (AstAnonClass*)node;
					return RetStatus(
					           RetStatusNor,
					           new Variable(
					               manager
					               ->MallocObject<ClassType>(
					                   ancl_node
					               )
					           )
					       );
				}

				RetStatus runAnonFunc(AstNode* node) {
					auto anfc_node = (AstAnonFunc*)node;
					return RetStatus(
					           RetStatusNor,
					           new Variable(
					               manager
					               ->MallocObject<MethodType>(
					                   -1, anfc_node, (ObjectType*)NULL
					               )
					           )

					       );
				}

				bool DataType2Bool(DataType* dt);	//用于条件判断

				RetStatus runForStatement(AstNode* node) {
					auto stm_node = (AstForStatement*)node;

					int iden = (
					               (AstIdentifier*)
					               node->Children()->at(0)
					           )
					           ->getID();	//遍历的标识符

					RetStatus list_st = RUN(node->Children()->at(1));

					DataType* list_dt = list_st.retval->data;

					OPND_PUSH(list_dt)

					CDT(list_dt, SequenceType)

					auto list = ((SequenceType*)list_dt)->getVal();

					for(int i=0,len=list.size(); i<len; i++) {

						manager->PushScope();

						Variable* iter = manager->NewVariable(
						                     iden, list[i]->data
						                 );

						//每次都新建一个标识符

						auto st = RUN(node->Children()->at(2));

						if(st.status==RetStatusBrk) {
							return RetStatus(RetStatusNor, NULL);
						}

						if(st.status==RetStatusRet) {
							return st;
						}

						manager->PopScope();

					}

					OPND_POP	//弹出list_dt

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runWhileStatement(AstNode* node) {
					auto stm_node = (AstWhileStatement*)node;

					RetStatus cond_st = RUN(node->Children()->at(0));

					DataType* cond = cond_st.retval->data;

					OPND_PUSH(cond)

					manager->PushScope();

					while(DataType2Bool(cond)==true) {
						RetStatus st = RUN(node->Children()->at(1));

						if(st.status==RetStatusBrk) {
							return RetStatus(RetStatusNor, NULL);
						}
						if(st.status==RetStatusRet) {
							return st;
						}

						OPND_POP	//弹出cond
						manager->PopScope();

						RetStatus cond_st = RUN(node->Children()->at(0));
						cond = cond_st.retval->data;

						OPND_PUSH(cond)
						manager->PushScope();
					}

					OPND_POP	//弹出cond
					manager->PopScope();

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runIfStatement(AstNode* node) {
					auto stm_node = (AstIfStatement*)node;

					RetStatus cond_st = RUN(node->Children()->at(0));

					DataType* cond = cond_st.retval->data;

					OPND_PUSH(cond)

					RetStatus st;

					if(DataType2Bool(cond)==true) {

						manager->PushScope();

						st = RUN(node->Children()->at(1));

						manager->PopScope();

					} else if(node->Children()->size()==3) {

						//有三个子节点，证明有else代码块

						manager->PushScope();

						st =RUN(node->Children()->at(2));

						manager->PopScope();

					} else {
						//直接略过本代码块
						st = RetStatus(RetStatusNor, NULL);
					}

					if(st.status==RetStatusBrk) {
						ThrowBreakError();
						CE
					}

					if(st.status==RetStatusCon) {
						ThrowContinueError();
						CE
					}

					if(st.status==RetStatusRet) {
						return st;
					}

					OPND_POP	//弹出cond

					return RetStatus(RetStatusNor, NULL);
				}

				RetStatus runReturnStatement(AstNode* node) {
					auto st = RUN(node->Children()->at(0));
					return RetStatus(RetStatusRet, st.retval);
				}

				RetStatus runSFN(AstNode* node) {
					int port = ((AstIdentifier*)node->Children()->at(0))
					           ->getID();
					int arg = ((AstIdentifier*)node->Children()->at(1))
					          ->getID();

					Variable* port_var = manager->GetVariable(port);
					CE
					Variable* arg_var = manager->GetVariable(arg);
					CE

					CDT(port_var->data, IntegerType)

					sfn.call(
					    (
					        (IntegerType*)port_var->data
					    )
					    ->getVal(),
					    arg_var
					);
					CE

					return RetStatus(RetStatusNor, NULL);
				}


				RetStatus runExpression(AstNode* node) {
					auto expr_node = (AstExpression*)node;
					if(expr_node->ass_type==-1) {
						return runAst(node->Children()->at(0));
					}

					//如果是赋值表达式

					//先分析左值
					Variable* left_value = runAst(node->Children()->at(0))
					                       .retval;
					CE

					OPND_PUSH(left_value->data)

					Variable* right_value = runAst(node->Children()->at(1))
					                        .retval;
					CE

					OPND_PUSH(right_value->data)

					if(expr_node->ass_type==TokenAssign) {
						left_value->data = right_value->data;
					}

					CHECK_ASS(Add, +,)
					CHECK_ASS(Sub, -,)
					CHECK_ASS(Mul, *,)
					CHECK_ASS(Div, /, DIV_ERRCHECK)
					CHECK_INT_ASS(Mod, %)
					CHECK_INT_ASS(And, &)
					CHECK_INT_ASS(XOr, ^)
					CHECK_INT_ASS(Or, |)
					CHECK_INT_ASS(LSH, <<)
					CHECK_INT_ASS(RSH, >>)

					OPND_POP
					OPND_POP

					return RetStatus(RetStatusNor, left_value);

				}

				RetStatus runLeftValue(AstNode* node) {
					auto lv_node = (AstLeftValue*)node;
					//获取标识符
					Variable* lvalue = manager->GetVariable(
					                       (
					                           (AstIdentifier*)
					                           node->Children()->at(0)
					                       )
					                       ->getID()
					                   );
					for(int i=1,len=node->Children()->size(); i<len; i++) {
						//分析后缀
						lvalue = runPostfix(
						             node->Children()->at(i), lvalue->data
						         )
						         .retval;
						CE
					}
					return RetStatus(RetStatusNor, lvalue);
				}

				RetStatus runBreak(AstNode* node) {
					return RetStatus(RetStatusBrk, NULL);
				}

				RetStatus runContinue(AstNode* node) {
					return RetStatus(RetStatusCon, NULL);
				}

				void BinaryOperatorConvert(
				    DataType*& left, DataType*& t1,
				    DataType*&right, DataType*& t2
				);

				RetStatus runBinary(AstNode* node) {
					AstBinary* bin_node = (AstBinary*)node;
					if(bin_node->getOperatorType()==-1) {
						RetStatus st = RUN(bin_node->Children()->at(0));
						return st;
					}
					OPERATE_BINARY(BitOR, |)
					OPERATE_BINARY(BitXOR, ^)
					OPERATE_BINARY(BitAND, &)
					OPERATE_BINARY(LeftShift, <<)
					OPERATE_BINARY(RightShift, >>)
					OPERATE_BINARY(Mod, %)

					if(bin_node->getOperatorType()==BinaryLogicORType) {
						RetStatus left_st = RUN(bin_node->Children()->at(0));
						DataType* left = left_st.retval->data;
						OPND_PUSH(left)

						RetStatus right_st = RUN(bin_node->Children()->at(1));
						DataType* right = right_st.retval->data;
						OPND_PUSH(right)

						DataType* rst;

						rst = right;	//默认返回右边

						//检查left，如果left为非零值和非空值，直接返回left即可
						//即逻辑短路

						if(left->getType()==IntegerTypeID) {
							if(((IntegerType*)left)->getVal()!=0) {
								rst = left;
							}
						} else if(left->getType()==FloatTypeID) {
							if(((FloatType*)left)->getVal()!=0) {
								rst = left;
							}
						} else if(left->getType()==DoubleTypeID) {
							if(((DoubleType*)left)->getVal()!=0) {
								rst = left;
							}
						} else {
							rst = left;
						}

						OPND_POP
						OPND_POP

						return RetStatus(RetStatusNor, new Variable(rst));
					}

					if(bin_node->getOperatorType()==BinaryLogicANDType) {
						RetStatus left_st = RUN(bin_node->Children()->at(0));
						DataType* left = left_st.retval->data;
						OPND_PUSH(left)

						RetStatus right_st = RUN(bin_node->Children()->at(1));
						DataType* right = right_st.retval->data;
						OPND_PUSH(right)

						DataType* rst = NULL;

						rst = right;	//默认返回右边


						//检查left，如果left为零值和空值，直接返回left即可
						//即逻辑短路

						if(left->getType()==NullTypeID) {
							rst = left;
						} else if(left->getType()==IntegerTypeID) {
							if(((IntegerType*)left)->getVal()==0) {
								rst = left;
							}
						} else if(left->getType()==FloatTypeID) {
							if(((FloatType*)left)->getVal()==0) {
								rst = left;
							}
						} else if(left->getType()==DoubleTypeID) {
							if(((DoubleType*)left)->getVal()==0) {
								rst = left;
							}
						}

						OPND_POP
						OPND_POP

						return RetStatus(RetStatusNor, new Variable(rst));
					}

					MATH_OPERATE(Add, +, )
					MATH_OPERATE(Sub, -, )
					MATH_OPERATE(Mult, *, )
					MATH_OPERATE(Divi, /, DIV_ERRCHECK)

					MATH_OPERATE(Equality, ==,)
					MATH_OPERATE(Inequality, !=,)
					MATH_OPERATE(BigThan, >,)
					MATH_OPERATE(LessThan, <,)
					MATH_OPERATE(BigThanOrEqual, >=,)
					MATH_OPERATE(LessThanOrEqual, <=,)

					ThrowUnknownOperatorError();

					return RetStatus(RetStatusErr, NULL);

				}

				RetStatus runUnary(AstNode* node) {
					AstUnary* unary_node = (AstUnary*)node;
					if(unary_node->getOperatorType()==-1) {
						//先分析quark
						DataType* quark;
						GETDT(quark, runAst(node->Children()->at(0)))
						//接着逐个分析后缀
						for(int i=1,len=node->Children()->size(); i<len; i++) {
							GETDT(
							    quark,
							    runPostfix(node->Children()->at(i), quark)
							)
						}
						return RetStatus(RetStatusErr, new Variable(quark));
					}

					//如果是单目运算符

					DataType* src;
					RetStatus rst(RetStatusErr, NULL);

					GETDT(src, runAst(node->Children()->at(0)));

					OPND_PUSH(src)

					if(unary_node->getOperatorType()==UnaryNotType) {
						//逻辑非
						//规定!(非零数)和!(null)为1，其余皆为0
						if(src->getType()==NullTypeID) {
							//!(null)为1
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<IntegerType>(1)
							          )
							      );

						} else if(src->getType()==IntegerTypeID) {
							//!(非零数)为0
							int v = ((IntegerType*)src)->getVal();
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<IntegerType>(!v)
							          )
							      );
						} else {
							//其余皆为0
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<IntegerType>(0)
							          )
							      );
							CE
						}
					}

					if(unary_node->getOperatorType()==UnaryNegative) {
						if(
						    !(
						        src->getType()==IntegerTypeID
						        ||src->getType()==FloatTypeID
						        ||src->getType()==DoubleTypeID
						    )
						) {
							//负数运算只能对数字进行
							ThrowTypeError(src->getType());
							rst = RetStatus(RetStatusErr, NULL);
						}

						if(src->getType()==IntegerTypeID) {
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<IntegerType>(
							                  -(
							                      ((IntegerType*)src)
							                      ->getVal()
							                  )
							              )
							          )
							      );
						}

						if(src->getType()==FloatTypeID) {
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<FloatType>(
							                  -(
							                      ((FloatType*)src)
							                      ->getVal()
							                  )
							              )
							          )
							      );
						}

						if(src->getType()==DoubleTypeID) {
							rst = RetStatus(
							          RetStatusNor,
							          new Variable(
							              manager
							              ->MallocObject<DoubleType>(
							                  -(
							                      ((DoubleType*)src)
							                      ->getVal()
							                  )
							              )
							          )
							      );
						}

					}

					if(src->getType()==UnaryPositiveType) {
						//正数运算无需任何处理
						if(
						    !(
						        src->getType()==IntegerTypeID
						        ||src->getType()==FloatTypeID
						        ||src->getType()==DoubleTypeID
						    )
						) {
							//正数运算只能对数字进行
							ThrowTypeError(src->getType());
							rst = RetStatus(RetStatusErr, NULL);
						}
					}

					if(src->getType()==UnaryPositiveType) {
						if(
						    !(
						        src->getType()==IntegerTypeID
						    )
						) {
							//取反运算只能对整数进行
							ThrowTypeError(src->getType());
						}

						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<IntegerType>(
						                  ~(
						                      ((IntegerType*)src)
						                      ->getVal()
						                  )
						              )
						          )
						      );
					}

					OPND_POP

					CE

					if(rst.status==RetStatusErr) {
						//说明没有分析到任何运算符
						ThrowUnknownOperatorError();
					}

					return rst;
				}

				RetStatus runPostfix(AstNode* node, DataType* src) {
					AstPostfix* postfix_node = (AstPostfix*)node;
					int postfix_type = postfix_node->getPostfixType();

					if(postfix_type==PostfixMemberType) {
						//访问成员
						CDT(src, ObjectType)
						AstIdentifier* member = (AstIdentifier*)
						                        postfix_node
						                        ->Children()
						                        ->at(0);

						int member_id = member->getID();

						if(
						    ((ObjectType*)src)
						    ->getVal()
						    .containsKey(member_id)==false
						) {
							//未知成员
							ThrowUnknownMemberError(member_id);
							CE
						}

						return RetStatus(
						           RetStatusNor,
						           ((ObjectType*)src)	//类对象
						           ->getVal()	//获取成员表
						           .get(member_id)	//获取成员
						       );
					}

					if(postfix_type==PostfixElementType) {
						//取下标
						CDT(src, SequenceType)
						AstExpression* expr = (AstExpression*)
						                      postfix_node
						                      ->Children()
						                      ->at(0);
						RetStatus st = RUN(expr);

						Variable* index_var = st.retval;

						DataType* index_dt = index_var->data;

						CDT(index_dt, IntegerType)

						ArrayList<Variable*> list = ((SequenceType*)src)
						                            ->getVal();

						int index = ((IntegerType*)index_dt)->getVal();

						if(index<0 ||index>=list.size()) {
							ThrowIndexError();
							return RetStatus(RetStatusErr, NULL);
						}

						return RetStatus(RetStatusNor, list[index]);

					}

					if(postfix_type==PostfixNewType) {
						CDT(src, ClassType)

						AstArguments* arg_ast = (AstArguments*)
						                        postfix_node
						                        ->Children()
						                        ->at(0);
						ArrayList<AstNode*>* arg = arg_ast->Children();

						OPND_PUSH(src)

						ObjectType* obj_dt = initObject((ClassType*)src);
						CE

						OPND_POP

						OPND_PUSH(obj_dt)

						if(obj_dt->getVal().containsKey(0)==true) {
							//有构造函数
							DataType* init_func_dt = obj_dt
							                         ->getVal()
							                         .get(0)
							                         ->data;
							CDT(init_func_dt, MethodType)
							runMethod((MethodType*)init_func_dt, arg);
							CE
						}

						OPND_POP

						return RetStatus(RetStatusNor, new Variable(obj_dt));

					}

					if(postfix_type==PostfixCallType) {
						//调用函数
						CDT(src, MethodType);
						AstArguments* arg_ast = (AstArguments*)
						                        postfix_node
						                        ->Children()
						                        ->at(0);
						ArrayList<AstNode*>* arg = arg_ast->Children();

						OPND_PUSH(src)

						auto st = runMethod((MethodType*)src, arg);
						CE

						OPND_POP

						return st;
					}

					ThrowPostfixError();
					return RetStatus(RetStatusErr, NULL);
				}

				RetStatus runArrayLiteral(AstNode* node) {
					AstArrayLiteral* literal = (AstArrayLiteral*)node;

					//获得长度
					RetStatus st = RUN(literal->Children()->at(0));

					DataType* length = st.retval->data;
					CDT(length, IntegerType)

					OPND_PUSH(length)

					Variable* rst_var = new Variable(
					    manager->MallocObject<SequenceType>(
					        ((IntegerType*)length)->getVal()
					    )
					);

					CE

					for(
					    int i=0,len=((IntegerType*)length)->getVal();
					    i<len;
					    i++
					) {
						((SequenceType*)rst_var->data)->sequence[i]
						    = new Variable(
						    manager->MallocObject<NullType>()
						);
					}

					CE

					OPND_POP

					return RetStatus(RetStatusNor, rst_var);
				}

				RetStatus runListLiteral(AstNode* node) {

					AstListLiteral* literal = (AstListLiteral*)node;
					ArrayList<Variable*> content;

					for (
					    int i=0,len=literal->Children()->size();
					    i<len;
					    i++
					) {
						RetStatus st = RUN(literal->Children()->at(i));

						DataType* item = st.retval->data;
						OPND_PUSH(item)
						content.add(new Variable(item));
					}


					Variable* rst_var = new Variable(
					    manager->MallocObject<SequenceType>(
					        content
					    )
					);

					CE

					for (
					    int i=0,len=literal->Children()->size();
					    i<len;
					    i++
					) {
						OPND_POP
					}	//弹出所有计算中的数据


					return RetStatus(RetStatusNor, rst_var);
				}

				RetStatus runIden(AstNode* node) {
					int index = ((AstLeaf*)node)->getVal();

					if(index>=tabconst.size()) {
						ThrowConstantsError();
						return RetStatus(RetStatusErr, NULL);
					}

					DataType* rst = tabconst[index];

					CDT(rst, IdenConstType)

					return RetStatus(RetStatusNor, new Variable(rst));
				}

				RetStatus runLeaf(AstNode* node) {
					int index = ((AstLeaf*)node)->getVal();
					if(index>=tabconst.size()) {
						ThrowConstantsError();
						return RetStatus(RetStatusErr, NULL);
					}

					DataType* rst = tabconst[index];

					if(rst->getType()==-1) {
						//标识符

						return RetStatus(
						           RetStatusNor, manager->GetVariable(index)
						       );
					}

					return RetStatus(
					           RetStatusNor, new Variable(rst)
					       );
				}

				RetStatus runNull(AstNode* node) {
					return RetStatus(
					           RetStatusNor,
					           new Variable(
					               manager
					               ->MallocObject<NullType>()
					           )
					       );
				}

		};
	}

}

//一些冗余的函数放到后面

inline String stamon::vm::AstRunner::getDataTypeName(int type) {
	if(type==ClassTypeID) {
		return String((char*)"class");
	}
	if(type==MethodTypeID) {
		return String((char*)"function");
	}
	if(type==NullTypeID) {
		return String((char*)"null");
	}

	if(type==IntegerTypeID) {
		return String((char*)"integer");
	}
	if(type==FloatTypeID) {
		return String((char*)"float");
	}
	if(type==DoubleTypeID) {
		return String((char*)"double");
	}
	if(type==ObjectTypeID) {
		return String((char*)"object");
	}
	if(type==SequenceTypeID) {
		return String((char*)"sequence");
	}
	if(type==StringTypeID) {
		return String((char*)"string");
	}
	if(type==-1) {
		return String((char*)"identifier");
	} else {
		return String((char*)"unknown-type");
	}
}


inline void stamon::vm::AstRunner::ThrowTypeError(int type) {
	THROW_S(
	    String((char*)"Type Error: "
	           "an error of data type \'")
	    + getDataTypeName(type)
	    + String((char*)"\' occurred in the calculation")
	)
}

inline void stamon::vm::AstRunner::ThrowPostfixError() {
	THROW("Postfix Error: unknown type of postfix")
}

inline void stamon::vm::AstRunner::ThrowIndexError() {
	THROW("Index Error: list index out of range")
}

inline void stamon::vm::AstRunner::ThrowConstantsError() {
	THROW("Constants Error: wrong index of constants")
}

inline void stamon::vm::AstRunner::ThrowDivZeroError() {
	THROW("Zero Division Error: division by zero")
}

inline void stamon::vm::AstRunner::ThrowBreakError() {
	THROW("Break Error: \'break\' outside loop")
}

inline void stamon::vm::AstRunner::ThrowContinueError() {
	THROW("Continue Error: \'continue\' outside loop")
}

inline void stamon::vm::AstRunner::ThrowArgumentsError(
    int form_args, int actual_args
) {
	THROW_S(
	    String((char*)"Arguments Error: takes ")
	    + toString(form_args)
	    + String((char*)" form arguments but ")
	    + toString(actual_args)
	    + String((char*)" was given")
	)
}

inline void stamon::vm::AstRunner::ThrowReturnError() {
	THROW("Return Error: \'return\' outside function")
}

inline void stamon::vm::AstRunner::ThrowUnknownOperatorError() {
	THROW("Operator Error: unknown operator")
}

inline void stamon::vm::AstRunner::ThrowUnknownMemberError(int id) {
	String iden = ((IdenConstType*)tabconst[id])->getVal();
	THROW_S(
	    String((char*)"Unknown Member Error: object has no member \'")
	    + iden
	    + String((char*)"\'")
	)
}

inline void stamon::vm::AstRunner::BinaryOperatorConvert(
    DataType*& left, DataType*& t1,
    DataType*&right, DataType*& t2
) {
	//先确定两者之间的最大优先级
	int priority_max = left->getType()>right->getType() ?
	                   left->getType() : right->getType();

	//先判断类型
	if(
	    !(
	        left->getType()==IntegerTypeID
	        ||left->getType()==FloatTypeID
	        ||left->getType()==DoubleTypeID
	    )
	) {
		ThrowTypeError(left->getType());
	}

	if(
	    !(
	        right->getType()==IntegerTypeID
	        ||right->getType()==FloatTypeID
	        ||right->getType()==DoubleTypeID
	    )
	) {
		ThrowTypeError(right->getType());
	}

	//接着进行繁杂的转换


	if(priority_max==IntegerTypeID) {
		t1 = manager->MallocObject<IntegerType>(
		         ((IntegerType*)left)->getVal()
		     );
		t2 = manager->MallocObject<IntegerType>(
		         ((IntegerType*)right)->getVal()
		     );
	}
	if(priority_max==FloatTypeID) {
		if(left->getType()==FloatTypeID) {
			if(right->getType()==IntegerTypeID) {
				t1 = manager->MallocObject<FloatType>(
				         ((FloatType*)left)->getVal()
				     );

				t2 = manager->MallocObject<FloatType>(
				         (float)
				         (
				             ((IntegerType*)right)
				             ->getVal()
				         )
				     );
			}
			//如果不是int，那就只能是float，即left和right皆为float
		}
		if(right->getType()==FloatTypeID) {
			if(left->getType()==IntegerTypeID) {
				t2 = manager->MallocObject<FloatType>(
				         ((FloatType*)left)->getVal()
				     );

				t1 = manager->MallocObject<FloatType>(
				         (float)
				         (
				             ((IntegerType*)left)
				             ->getVal()
				         )
				     );
			}
			//如果不是int，那就只能是float，即left和right皆为float
		}
	}

	if(priority_max==DoubleTypeID) {
		if(left->getType()==IntegerTypeID) {
			t1 = manager->MallocObject<DoubleType>(
			         (double)
			         (
			             ((IntegerType*)left)
			             ->getVal()
			         )
			     );
		} else if(left->getType()==FloatTypeID) {
			t1 = manager->MallocObject<DoubleType>(
			         (double)
			         (
			             ((FloatType*)left)
			             ->getVal()
			         )
			     );
		} else {
			t1 = manager->MallocObject<DoubleType>(
			         ((DoubleType*)left)->getVal()
			     );
		}
		if(right->getType()==IntegerTypeID) {
			t2 = manager->MallocObject<DoubleType>(
			         (double)
			         (
			             ((IntegerType*)right)
			             ->getVal()
			         )
			     );
		} else if(right->getType()==FloatTypeID) {
			t2 = manager->MallocObject<DoubleType>(
			         (double)
			         (
			             ((FloatType*)right)
			             ->getVal()
			         )
			     );
		} else {
			t2 = manager->MallocObject<DoubleType>(
			         ((DoubleType*)right)->getVal()
			     );
		}
	}

	return;
}

bool stamon::vm::AstRunner::DataType2Bool(DataType* dt) {
	if(dt->getType()==NullTypeID) {
		return false;
	}
	if(dt->getType()==IntegerTypeID) {
		return ((IntegerType*)dt)->getVal() != 0 ? true : false;
	}
	if(dt->getType()==FloatTypeID) {
		return ((FloatType*)dt)->getVal() != 0 ? true : false;
	}
	if(dt->getType()==DoubleTypeID) {
		return ((DoubleType*)dt)->getVal() != 0 ? true : false;
	}

	return true;
}

#undef CDT
#undef OPND_PUSH
#undef OPND_POP
#undef RUN
#undef GETDT
#undef CTH
#undef CTH_S
#undef OPERATE_BINARY
#undef ASMD_OPERATE
#undef DIV_ERRCHECK
#undef MATH_OPERATE
#undef BIND
#undef CHECK_ASS
#undef CHECK_INT_ASS

#endif