/*
	Name: ast::AstRunner.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 11/02/24 14:16
	Description: 语法树的运行器
*/

#pragma once

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
//Get datatype::DataType，想要安全的获取datatype::DataType，应该使用这个宏


#define CE			CATCH { return RetStatus(RetStatusErr, NULL); }
//如果执行代码中出现异常，直接返回
#define CTH(message)	CATCH { THROW(message) }
//如果执行代码中出现异常，抛出异常
#define CTH_S(message)	CATCH { THROW_S(message) }

#define OPERATE_BINARY(type, op) \
	if(bin_node->getOperatorType()==ast::Binary##type##Type) {\
		RetStatus left_st = RUN(bin_node->Children()->at(0));\
		datatype::DataType* left = left_st.retval->data;\
		CDT(left, datatype::IntegerType)\
		OPND_PUSH(left)\
		\
		RetStatus right_st = RUN(bin_node->Children()->at(1));\
		datatype::DataType* right = right_st.retval->data;\
		CDT(left, datatype::IntegerType)\
		OPND_PUSH(right)\
		\
		datatype::DataType* rst = manager->MallocObject<datatype::IntegerType>(\
		                          ((datatype::IntegerType*)left)->getVal()\
		                          op ((datatype::IntegerType*)right)->getVal()\
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
	if(t1->getType()==datatype::IntegerTypeID) {\
		auto l = (datatype::IntegerType*)t1;\
		auto r = (datatype::IntegerType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<datatype::IntegerType>(\
		        l->getVal() op r->getVal()\
		                                                  );\
	} else if(t1->getType()==datatype::FloatTypeID) {\
		auto l = (datatype::FloatType*)t1;\
		auto r = (datatype::FloatType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<datatype::FloatType>(\
		        l->getVal() op r->getVal()\
		                                                );\
	} else if(t1->getType()==datatype::DoubleTypeID) {\
		auto l = (datatype::DoubleType*)t1;\
		auto r = (datatype::DoubleType*)t2;\
		ErrCheck\
		CE\
		rst = manager->MallocObject<datatype::DoubleType>(\
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
	if(bin_node->getOperatorType()==ast::Binary##type##Type) {\
		RetStatus left_st = RUN(bin_node->Children()->at(0));\
		datatype::DataType* left = left_st.retval->data;\
		OPND_PUSH(left)\
		\
		RetStatus right_st = RUN(bin_node->Children()->at(1));\
		datatype::DataType* right = right_st.retval->data;\
		OPND_PUSH(right)\
		\
		datatype::DataType* t1;\
		datatype::DataType* t2;\
		BinaryOperatorConvert(left, t1, right, t2);\
		CE\
		\
		datatype::DataType* rst;\
		\
		ASMD_OPERATE(op, ErrCheck)\
		\
		OPND_POP\
		OPND_POP\
		\
		return RetStatus(RetStatusNor, new Variable(rst));\
	}

#define BIND(name) RunAstFunc[ast::Ast##name##Type] = &AstRunner::run##name;

#define CHECK_ASS(op_type, op, ErrCheck) \
	if(expr_node->ass_type==c::Token##op_type##Ass) {\
		datatype::DataType* t1;\
		datatype::DataType* t2;\
		BinaryOperatorConvert(left_value->data, t1, right_value->data, t2);\
		OPND_PUSH(t1);\
		OPND_PUSH(t2);\
		if(t1->getType()==datatype::IntegerTypeID) {\
			auto l = (datatype::IntegerType*)t1;\
			auto r = (datatype::IntegerType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<datatype::IntegerType>(\
			                   l->getVal() op r->getVal()\
			                                                               );\
		} else if(t1->getType()==datatype::FloatTypeID) {\
			auto l = (datatype::FloatType*)t1;\
			auto r = (datatype::FloatType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<datatype::FloatType>(\
			                   l->getVal() op r->getVal()\
			                                                             );\
		} else if(t1->getType()==datatype::DoubleTypeID) {\
			auto l = (datatype::DoubleType*)t1;\
			auto r = (datatype::DoubleType*)t2;\
			ErrCheck\
			CE\
			left_value->data = manager->MallocObject<datatype::DoubleType>(\
			                   l->getVal() op r->getVal()\
			                                                              );\
		}\
		CE\
		OPND_POP\
		OPND_POP\
	}


#define CHECK_INT_ASS(op_type, op) \
	if(expr_node->ass_type==c::Token##op_type##Ass) {\
		CDT(left_value->data, datatype::IntegerType)\
		CDT(right_value->data, datatype::IntegerType)\
		auto t1 = manager->MallocObject<datatype::IntegerType>(\
		          ((datatype::IntegerType*)left_value->data)->getVal()\
		                                                      );\
		OPND_PUSH(t1)\
		auto t2 = manager->MallocObject<datatype::IntegerType>(\
		          ((datatype::IntegerType*)right_value->data)->getVal()\
		                                                      );\
		OPND_PUSH(t2)\
		left_value->data = manager\
		                   ->MallocObject<datatype::IntegerType>(\
		                           t1->getVal() op t2->getVal()\
		                                                        );\
		OPND_POP\
		OPND_POP\
		CE\
	}

namespace stamon::vm {

	typedef datatype::Variable Variable;

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
			RetStatus(AstRunner::*RunAstFunc[ast::AstTypeNum])
			(ast::AstNode* node);
			//由类成员函数指针组成的数组
			ast::AstNode* program;
			bool is_gc;	//是否允许gc
			int gc_mem_limit;	//对象内存最大限制
			ArrayList<datatype::DataType*> tabconst;	//常量表
			ArrayList<String> vm_args;	//虚拟机参数
			STMException* ex;	//异常
			sfn::SFN sfn;

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
			 * \param main_node 虚拟机的入口ast节点，即ast::AstProgram
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
			    ast::AstNode* main_node, bool isGC, int vm_mem_limit,
			    ArrayList<datatype::DataType*> tableConst,
			    ArrayList<String> args,STMException* e
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

				sfn = sfn::SFN(e, manager);

				//执行程序
				auto st = runAst(program);
				//释放内存

				delete manager;

				return st;
			}

			RetStatus runAst(ast::AstNode* node) {
				RunningLineNo = node->lineNo;
				RunningFileName = node->filename;

				if(node->getType()==-1) {
					//叶子节点
					return runLeaf(node);
				} else {
					return (this->*(RunAstFunc[node->getType()]))(node);
				}
			}

			RetStatus runProgram(ast::AstNode* node) {
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

			datatype::ObjectType* initObject(datatype::ClassType* cls) {
				datatype::ObjectType* rst;
				NumberMap<Variable> membertab;	//成员表
				auto node = cls->getVal();

				/*处理父类*/
				if(node->isHaveFather == true) {
					//有父类，先初始化父类
					auto father_class = manager->GetVariable(
					                        (
					                            (ast::AstIdentifier*)
					                            node
					                            ->Children()
					                            ->at(0)
					                        )
					                        ->getID()
					                    );

					if(father_class->data->getType()!=datatype::ClassTypeID) {
						ThrowTypeError(father_class->data->getType());
						return NULL;
					}

					rst = initObject((datatype::ClassType*)father_class);
					CATCH {
						return NULL;
					}

					membertab.destroy();	//先销毁之前创建的空表

					membertab = rst->getVal();

				} else {
					rst = manager
					      ->MallocObject<datatype::ObjectType>(membertab);

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

					if(n->getType()==ast::AstDefClassType) {
						runDefClass(n);
					} else if(n->getType()==ast::AstDefFuncType) {
						runDefMethod(rst, n);
					} else if(n->getType()==ast::AstDefVarType) {
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
			    datatype::MethodType* method, ArrayList<ast::AstNode*>* args
			) {
				OPND_PUSH(method);

				/*返回的结果，默认返回null*/
				datatype::DataType* rst = manager
				                          ->MallocObject<datatype::NullType>();
				CE

				OPND_PUSH(rst);

				/*先获取容器*/
				auto container = method->getContainer();

				/*再获得形参*/
				auto FormArg = method->getVal()->Children()->clone();
				FormArg.erase(FormArg.size()-1);	//删除ast::AstBlock，只保留参数

				/*接着计算实际参数的表达式*/
				ArrayList<datatype::DataType*> args_val;

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
					        (ast::AstIdentifier*)FormArg.at(i)
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
				 * 我想要运行函数体（即ast::AstBlock）
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

			RetStatus runBlock(ast::AstNode* node) {
				auto blk_node = (ast::AstBlock*)node;
				ArrayList<ast::AstNode*>* codes = node->Children();

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

			RetStatus runDefVar(ast::AstNode* node) {
				auto val = RUN(node->Children()->at(1));
				manager->NewVariable(
				    (
				        (ast::AstIdentifier*)node->Children()->at(0)
				    )
				    ->getID(),
				    val.retval->data
				);
				return RetStatus(RetStatusNor, NULL);
			}

			datatype::MethodType* runDefMethod(
			    datatype::ObjectType* container, ast::AstNode* node) {

				auto iden = (
				                (ast::AstIdentifier*)node->Children()->at(0)
				            )
				            ->getID();

				RetStatus st = runAst(node->Children()->at(1));

				CATCH {
					return NULL;
				}

				auto func = (datatype::MethodType*)st.retval->data;

				func->id = iden;
				func->container = container;

				manager->NewVariable(iden, func);

				return func;
			}

			RetStatus runDefFunc(ast::AstNode* node) {
				auto iden = (
				                (ast::AstIdentifier*)node->Children()->at(0)
				            )
				            ->getID();

				auto st = RUN(node->Children()->at(1));

				auto func = (datatype::MethodType*)st.retval->data;

				func->id = iden;

				manager->NewVariable(iden, func);

				return RetStatus(RetStatusNor, NULL);
			}

			RetStatus runDefClass(ast::AstNode* node) {
				auto iden = (
				                (ast::AstIdentifier*)node->Children()->at(0)
				            )
				            ->getID();
				auto st = RUN(node->Children()->at(1));

				auto cls = (datatype::ClassType*)st.retval->data;

				manager->NewVariable(iden, cls);

				return RetStatus(RetStatusNor, NULL);
			}

			RetStatus runAnonClass(ast::AstNode* node) {
				auto ancl_node = (ast::AstAnonClass*)node;
				return RetStatus(
				           RetStatusNor,
				           new Variable(
				               manager
				               ->MallocObject<datatype::ClassType>(
				                   ancl_node
				               )
				           )
				       );
			}

			RetStatus runAnonFunc(ast::AstNode* node) {
				auto anfc_node = (ast::AstAnonFunc*)node;
				return RetStatus(
				           RetStatusNor,
				           new Variable(
				               manager
				               ->MallocObject<datatype::MethodType>(
				                   -1, anfc_node, (datatype::ObjectType*)NULL
				               )
				           )

				       );
			}

			bool DataType2Bool(datatype::DataType* dt);	//用于条件判断

			RetStatus runForStatement(ast::AstNode* node) {
				auto stm_node = (ast::AstForStatement*)node;

				int iden = (
				               (ast::AstIdentifier*)
				               node->Children()->at(0)
				           )
				           ->getID();	//遍历的标识符

				RetStatus list_st = RUN(node->Children()->at(1));

				datatype::DataType* list_dt = list_st.retval->data;

				OPND_PUSH(list_dt)

				CDT(list_dt, datatype::SequenceType)

				auto list = ((datatype::SequenceType*)list_dt)->getVal();

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

			RetStatus runWhileStatement(ast::AstNode* node) {
				auto stm_node = (ast::AstWhileStatement*)node;

				RetStatus cond_st = RUN(node->Children()->at(0));

				datatype::DataType* cond = cond_st.retval->data;

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

			RetStatus runIfStatement(ast::AstNode* node) {
				auto stm_node = (ast::AstIfStatement*)node;

				RetStatus cond_st = RUN(node->Children()->at(0));

				datatype::DataType* cond = cond_st.retval->data;

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

			RetStatus runReturnStatement(ast::AstNode* node) {
				auto st = RUN(node->Children()->at(0));
				return RetStatus(RetStatusRet, st.retval);
			}

			RetStatus runSFN(ast::AstNode* node) {
				int port = ((ast::AstIdentifier*)node->Children()->at(0))
				           ->getID();
				int arg = ((ast::AstIdentifier*)node->Children()->at(1))
				          ->getID();

				Variable* port_var = manager->GetVariable(port);
				CE
				Variable* arg_var = manager->GetVariable(arg);
				CE

				CDT(port_var->data, datatype::StringType)

				sfn.call(
				    (
				        (datatype::StringType*)port_var->data
				    )
				    ->getVal(),
				    arg_var
				);
				CE

				return RetStatus(RetStatusNor, NULL);
			}


			RetStatus runExpression(ast::AstNode* node) {
				auto expr_node = (ast::AstExpression*)node;
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

				if(expr_node->ass_type==c::TokenAssign) {
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

			RetStatus runLeftValue(ast::AstNode* node) {
				auto lv_node = (ast::AstLeftValue*)node;
				//获取标识符
				Variable* lvalue = manager->GetVariable(
				                       (
				                           (ast::AstIdentifier*)
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

			RetStatus runBreak(ast::AstNode* node) {
				return RetStatus(RetStatusBrk, NULL);
			}

			RetStatus runContinue(ast::AstNode* node) {
				return RetStatus(RetStatusCon, NULL);
			}

			void BinaryOperatorConvert(
			    datatype::DataType*& left, datatype::DataType*& t1,
			    datatype::DataType*&right, datatype::DataType*& t2
			);

			RetStatus runBinary(ast::AstNode* node) {
				ast::AstBinary* bin_node = (ast::AstBinary*)node;
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

				if(bin_node->getOperatorType()==ast::BinaryLogicORType) {
					RetStatus left_st = RUN(bin_node->Children()->at(0));
					datatype::DataType* left = left_st.retval->data;
					OPND_PUSH(left)

					RetStatus right_st = RUN(bin_node->Children()->at(1));
					datatype::DataType* right = right_st.retval->data;
					OPND_PUSH(right)

					datatype::DataType* rst;

					rst = right;	//默认返回右边

					//检查left，如果left为非零值和非空值，直接返回left即可
					//即逻辑短路

					if(left->getType()==datatype::IntegerTypeID) {
						if(((datatype::IntegerType*)left)->getVal()!=0) {
							rst = left;
						}
					} else if(left->getType()==datatype::FloatTypeID) {
						if(((datatype::FloatType*)left)->getVal()!=0) {
							rst = left;
						}
					} else if(left->getType()==datatype::DoubleTypeID) {
						if(((datatype::DoubleType*)left)->getVal()!=0) {
							rst = left;
						}
					} else {
						rst = left;
					}

					OPND_POP
					OPND_POP

					return RetStatus(RetStatusNor, new Variable(rst));
				}

				if(bin_node->getOperatorType()==ast::BinaryLogicANDType) {
					RetStatus left_st = RUN(bin_node->Children()->at(0));
					datatype::DataType* left = left_st.retval->data;
					OPND_PUSH(left)

					RetStatus right_st = RUN(bin_node->Children()->at(1));
					datatype::DataType* right = right_st.retval->data;
					OPND_PUSH(right)

					datatype::DataType* rst = NULL;

					rst = right;	//默认返回右边


					//检查left，如果left为零值和空值，直接返回left即可
					//即逻辑短路

					if(left->getType()==datatype::NullTypeID) {
						rst = left;
					} else if(left->getType()==datatype::IntegerTypeID) {
						if(((datatype::IntegerType*)left)->getVal()==0) {
							rst = left;
						}
					} else if(left->getType()==datatype::FloatTypeID) {
						if(((datatype::FloatType*)left)->getVal()==0) {
							rst = left;
						}
					} else if(left->getType()==datatype::DoubleTypeID) {
						if(((datatype::DoubleType*)left)->getVal()==0) {
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

			RetStatus runUnary(ast::AstNode* node) {
				ast::AstUnary* unary_node = (ast::AstUnary*)node;
				if(unary_node->getOperatorType()==-1) {
					//先分析quark
					datatype::DataType* quark;
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

				datatype::DataType* src;
				RetStatus rst(RetStatusErr, NULL);

				GETDT(src, runAst(node->Children()->at(0)));

				OPND_PUSH(src)

				if(unary_node->getOperatorType()==ast::UnaryNotType) {
					//逻辑非
					//规定!(非零数)和!(null)为1，其余皆为0
					if(src->getType()==datatype::NullTypeID) {
						//!(null)为1
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::IntegerType>(1)
						          )
						      );

					} else if(src->getType()==datatype::IntegerTypeID) {
						//!(非零数)为0
						int v = ((datatype::IntegerType*)src)->getVal();
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::IntegerType>(!v)
						          )
						      );
					} else {
						//其余皆为0
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::IntegerType>(0)
						          )
						      );
						CE
					}
				}

				if(unary_node->getOperatorType()==ast::UnaryNegative) {
					if(
					    !(
					        src->getType()==datatype::IntegerTypeID
					        ||src->getType()==datatype::FloatTypeID
					        ||src->getType()==datatype::DoubleTypeID
					    )
					) {
						//负数运算只能对数字进行
						ThrowTypeError(src->getType());
						rst = RetStatus(RetStatusErr, NULL);
					}

					if(src->getType()==datatype::IntegerTypeID) {
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::IntegerType>(
						                  -(
						                      ((datatype::IntegerType*)src)
						                      ->getVal()
						                  )
						              )
						          )
						      );
					}

					if(src->getType()==datatype::FloatTypeID) {
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::FloatType>(
						                  -(
						                      ((datatype::FloatType*)src)
						                      ->getVal()
						                  )
						              )
						          )
						      );
					}

					if(src->getType()==datatype::DoubleTypeID) {
						rst = RetStatus(
						          RetStatusNor,
						          new Variable(
						              manager
						              ->MallocObject<datatype::DoubleType>(
						                  -(
						                      ((datatype::DoubleType*)src)
						                      ->getVal()
						                  )
						              )
						          )
						      );
					}

				}

				if(src->getType()==ast::UnaryPositiveType) {
					//正数运算无需任何处理
					if(
					    !(
					        src->getType()==datatype::IntegerTypeID
					        ||src->getType()==datatype::FloatTypeID
					        ||src->getType()==datatype::DoubleTypeID
					    )
					) {
						//正数运算只能对数字进行
						ThrowTypeError(src->getType());
						rst = RetStatus(RetStatusErr, NULL);
					}
				}

				if(src->getType()==ast::UnaryPositiveType) {
					if(
					    !(
					        src->getType()==datatype::IntegerTypeID
					    )
					) {
						//取反运算只能对整数进行
						ThrowTypeError(src->getType());
					}

					rst = RetStatus(
					          RetStatusNor,
					          new Variable(
					              manager
					              ->MallocObject<datatype::IntegerType>(
					                  ~(
					                      ((datatype::IntegerType*)src)
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

			RetStatus runPostfix(ast::AstNode* node, datatype::DataType* src) {
				ast::AstPostfix* postfix_node = (ast::AstPostfix*)node;
				int postfix_type = postfix_node->getPostfixType();

				if(postfix_type==ast::PostfixMemberType) {
					//访问成员
					CDT(src, datatype::ObjectType)
					ast::AstIdentifier* member = (ast::AstIdentifier*)
					                             postfix_node
					                             ->Children()
					                             ->at(0);

					int member_id = member->getID();

					if(
					    ((datatype::ObjectType*)src)
					    ->getVal()
					    .containsKey(member_id)==false
					) {
						//未知成员
						ThrowUnknownMemberError(member_id);
						CE
					}

					return RetStatus(
					           RetStatusNor,
					           ((datatype::ObjectType*)src)	//类对象
					           ->getVal()	//获取成员表
					           .get(member_id)	//获取成员
					       );
				}

				if(postfix_type==ast::PostfixElementType) {
					//取下标
					CDT(src, datatype::SequenceType)
					ast::AstExpression* expr = (ast::AstExpression*)
					                           postfix_node
					                           ->Children()
					                           ->at(0);
					RetStatus st = RUN(expr);

					Variable* index_var = st.retval;

					datatype::DataType* index_dt = index_var->data;

					CDT(index_dt, datatype::IntegerType)

					ArrayList<Variable*> list = ((datatype::SequenceType*)src)
					                            ->getVal();

					int index = ((datatype::IntegerType*)index_dt)->getVal();

					if(index<0 ||index>=list.size()) {
						ThrowIndexError();
						return RetStatus(RetStatusErr, NULL);
					}

					return RetStatus(RetStatusNor, list[index]);

				}

				if(postfix_type==ast::PostfixNewType) {
					CDT(src, datatype::ClassType)

					ast::AstArguments* arg_ast = (ast::AstArguments*)
					                             postfix_node
					                             ->Children()
					                             ->at(0);
					ArrayList<ast::AstNode*>* arg = arg_ast->Children();

					OPND_PUSH(src)

					datatype::ObjectType* obj_dt = initObject(
					                                   (datatype::ClassType*)src
					                               );
					CE

					OPND_POP

					OPND_PUSH(obj_dt)

					if(obj_dt->getVal().containsKey(0)==true) {
						//有构造函数
						datatype::DataType* init_func_dt = obj_dt
						                                   ->getVal()
						                                   .get(0)
						                                   ->data;
						CDT(init_func_dt, datatype::MethodType)
						runMethod((datatype::MethodType*)init_func_dt, arg);
						CE
					}

					OPND_POP

					return RetStatus(RetStatusNor, new Variable(obj_dt));

				}

				if(postfix_type==ast::PostfixCallType) {
					//调用函数
					CDT(src, datatype::MethodType);
					ast::AstArguments* arg_ast = (ast::AstArguments*)
					                             postfix_node
					                             ->Children()
					                             ->at(0);
					ArrayList<ast::AstNode*>* arg = arg_ast->Children();

					OPND_PUSH(src)

					auto st = runMethod((datatype::MethodType*)src, arg);
					CE

					OPND_POP

					return st;
				}

				ThrowPostfixError();
				return RetStatus(RetStatusErr, NULL);
			}

			RetStatus runArrayLiteral(ast::AstNode* node) {
				ast::AstArrayLiteral* literal = (ast::AstArrayLiteral*)node;

				//获得长度
				RetStatus st = RUN(literal->Children()->at(0));

				datatype::DataType* length = st.retval->data;
				CDT(length, datatype::IntegerType)

				OPND_PUSH(length)

				Variable* rst_var = new Variable(
				    manager->MallocObject<datatype::SequenceType>(
				        ((datatype::IntegerType*)length)->getVal()
				    )
				);

				CE

				for(
				    int i=0,len=((datatype::IntegerType*)length)->getVal();
				    i<len;
				    i++
				) {
					((datatype::SequenceType*)rst_var->data)->sequence[i]
					    = new Variable(
					    manager->MallocObject<datatype::NullType>()
					);
				}

				CE

				OPND_POP

				return RetStatus(RetStatusNor, rst_var);
			}

			RetStatus runListLiteral(ast::AstNode* node) {

				ast::AstListLiteral* literal = (ast::AstListLiteral*)node;
				ArrayList<Variable*> content;

				for (
				    int i=0,len=literal->Children()->size();
				    i<len;
				    i++
				) {
					RetStatus st = RUN(literal->Children()->at(i));

					datatype::DataType* item = st.retval->data;
					OPND_PUSH(item)
					content.add(new Variable(item));
				}


				Variable* rst_var = new Variable(
				    manager->MallocObject<datatype::SequenceType>(
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

			RetStatus runIden(ast::AstNode* node) {
				int index = ((ir::AstLeaf*)node)->getVal();

				if(index>=tabconst.size()) {
					ThrowConstantsError();
					return RetStatus(RetStatusErr, NULL);
				}

				datatype::DataType* rst = tabconst[index];

				CDT(rst, ir::IdenConstType)

				return RetStatus(RetStatusNor, new Variable(rst));
			}

			RetStatus runLeaf(ast::AstNode* node) {
				int index = ((ir::AstLeaf*)node)->getVal();
				if(index>=tabconst.size()) {
					ThrowConstantsError();
					return RetStatus(RetStatusErr, NULL);
				}

				datatype::DataType* rst = tabconst[index];

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

			RetStatus runNull(ast::AstNode* node) {
				return RetStatus(
				           RetStatusNor,
				           new Variable(
				               manager
				               ->MallocObject<datatype::NullType>()
				           )
				       );
			}

	};
} //namespace stamon::vm

//一些冗余的函数放到后面

inline String stamon::vm::AstRunner::getDataTypeName(int type) {
	if(type==stamon::datatype::ClassTypeID) {
		return String((char*)"class");
	}
	if(type==stamon::datatype::MethodTypeID) {
		return String((char*)"function");
	}
	if(type==stamon::datatype::NullTypeID) {
		return String((char*)"null");
	}

	if(type==stamon::datatype::IntegerTypeID) {
		return String((char*)"integer");
	}
	if(type==stamon::datatype::FloatTypeID) {
		return String((char*)"float");
	}
	if(type==stamon::datatype::DoubleTypeID) {
		return String((char*)"double");
	}
	if(type==stamon::datatype::ObjectTypeID) {
		return String((char*)"object");
	}
	if(type==stamon::datatype::SequenceTypeID) {
		return String((char*)"sequence");
	}
	if(type==stamon::datatype::StringTypeID) {
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
	THROW("ast::Postfix Error: unknown type of postfix")
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
	String iden = ((ir::IdenConstType*)tabconst[id])->getVal();
	THROW_S(
	    String((char*)"Unknown Member Error: object has no member \'")
	    + iden
	    + String((char*)"\'")
	)
}

inline void stamon::vm::AstRunner::BinaryOperatorConvert(
    datatype::DataType*& left, datatype::DataType*& t1,
    datatype::DataType*&right, datatype::DataType*& t2
) {
	//先确定两者之间的最大优先级
	int priority_max = left->getType()>right->getType() ?
	                   left->getType() : right->getType();

	//先判断类型
	if(
	    !(
	        left->getType()==datatype::IntegerTypeID
	        ||left->getType()==datatype::FloatTypeID
	        ||left->getType()==datatype::DoubleTypeID
	    )
	) {
		ThrowTypeError(left->getType());
	}

	if(
	    !(
	        right->getType()==datatype::IntegerTypeID
	        ||right->getType()==datatype::FloatTypeID
	        ||right->getType()==datatype::DoubleTypeID
	    )
	) {
		ThrowTypeError(right->getType());
	}

	//接着进行繁杂的转换


	if(priority_max==datatype::IntegerTypeID) {
		t1 = manager->MallocObject<datatype::IntegerType>(
		         ((datatype::IntegerType*)left)->getVal()
		     );
		t2 = manager->MallocObject<datatype::IntegerType>(
		         ((datatype::IntegerType*)right)->getVal()
		     );
	}
	if(priority_max==datatype::FloatTypeID) {
		if(left->getType()==datatype::FloatTypeID) {
			if(right->getType()==datatype::IntegerTypeID) {
				t1 = manager->MallocObject<datatype::FloatType>(
				         ((datatype::FloatType*)left)->getVal()
				     );

				t2 = manager->MallocObject<datatype::FloatType>(
				         (float)
				         (
				             ((datatype::IntegerType*)right)
				             ->getVal()
				         )
				     );
			}
			//如果不是int，那就只能是float，即left和right皆为float
		}
		if(right->getType()==datatype::FloatTypeID) {
			if(left->getType()==datatype::IntegerTypeID) {
				t2 = manager->MallocObject<datatype::FloatType>(
				         ((datatype::FloatType*)left)->getVal()
				     );

				t1 = manager->MallocObject<datatype::FloatType>(
				         (float)
				         (
				             ((datatype::IntegerType*)left)
				             ->getVal()
				         )
				     );
			}
			//如果不是int，那就只能是float，即left和right皆为float
		}
	}

	if(priority_max==datatype::DoubleTypeID) {
		if(left->getType()==datatype::IntegerTypeID) {
			t1 = manager->MallocObject<datatype::DoubleType>(
			         (double)
			         (
			             ((datatype::IntegerType*)left)
			             ->getVal()
			         )
			     );
		} else if(left->getType()==datatype::FloatTypeID) {
			t1 = manager->MallocObject<datatype::DoubleType>(
			         (double)
			         (
			             ((datatype::FloatType*)left)
			             ->getVal()
			         )
			     );
		} else {
			t1 = manager->MallocObject<datatype::DoubleType>(
			         ((datatype::DoubleType*)left)->getVal()
			     );
		}
		if(right->getType()==datatype::IntegerTypeID) {
			t2 = manager->MallocObject<datatype::DoubleType>(
			         (double)
			         (
			             ((datatype::IntegerType*)right)
			             ->getVal()
			         )
			     );
		} else if(right->getType()==datatype::FloatTypeID) {
			t2 = manager->MallocObject<datatype::DoubleType>(
			         (double)
			         (
			             ((datatype::FloatType*)right)
			             ->getVal()
			         )
			     );
		} else {
			t2 = manager->MallocObject<datatype::DoubleType>(
			         ((datatype::DoubleType*)right)->getVal()
			     );
		}
	}

	return;
}

bool stamon::vm::AstRunner::DataType2Bool(datatype::DataType* dt) {
	if(dt->getType()==datatype::NullTypeID) {
		return false;
	}
	if(dt->getType()==datatype::IntegerTypeID) {
		return ((datatype::IntegerType*)dt)->getVal() != 0 ? true : false;
	}
	if(dt->getType()==datatype::FloatTypeID) {
		return ((datatype::FloatType*)dt)->getVal() != 0 ? true : false;
	}
	if(dt->getType()==datatype::DoubleTypeID) {
		return ((datatype::DoubleType*)dt)->getVal() != 0 ? true : false;
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