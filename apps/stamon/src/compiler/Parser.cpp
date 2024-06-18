/*
	Name: Parser.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 05/01/24 21:33
	Description: 语法分析器
*/

#ifndef PARSER_CPP
#define PARSER_CPP

#include"Ast.hpp"
#include"Stack.hpp"
#include"StringMap.hpp"
#include"FileMap.hpp"

#include"Lexer.cpp"

#define check(type) (matcher.Check(type))
#define _pop			(matcher.Pop())
#define CE			CATCH { return NULL; }	//如果执行代码中出现异常，直接返回
#define CTH(message)	CATCH { THROW(message) } //如果执行代码中出现异常，抛出异常
#define GETLN(type) \
	int lineNo;\
	if(true) {\
		Token* tok = match(type);\
		CE\
		lineNo = tok->lineNo;\
	}

/*
 * GETLN的全写是：Get LineNo
 * 这个宏会匹配一个类型为type的token，并且把行号存入lineNo
 * 套一层if(true)的作用是：防止后续的代码也定义了tok导致编译器报错
 	* 套一层if(true)之后，tok这个变量会随着if(true)这个作用域的消失而消失
*/

#define pushscope(wall) \
	if(true) {\
		SyntaxScope scope(ex);\
		scope.isWall = wall;\
		scopes.add(scope);\
	}
//这里if(true)的作用同GETLN
#define popscope (scopes.erase(scopes.size()-1))


#define unary_check(tok_type, unary_type) \
	if(check(tok_type)) {\
		int line = _pop->lineNo;\
		return Ast<AstUnary>(\
		                     line,\
		                     unary_type,\
		                     unary_operator()\
		                    );\
	}

//为了方便编写语法分析的代码，我设计了这些宏
//这些宏只能在这个文件中被使用



namespace stamon {
	namespace c {

		using namespace stamon::ast;

		class Matcher {
				//用于在语法分析中查看并匹配token
				Lexer lexer;

			public:

				STMException* ex;

				Matcher() {}

				Matcher(Lexer lex, STMException* e) {
					lexer = lex;
					ex = e;
				}

				bool Check(int type) {
					//用于检查token的类型
					if(lexer.peek(0)->type==type) {
						return true;
					}
					return false;
				}

				Token* Match(int type) {
					//匹配一个类型为type的token，如果不匹配，就直接报错
					if(Check(type)) {
						return lexer.getTok();
					} else {
						THROW("invalid syntax")
						return NULL;
					}
				}

				Token* Peek(int index) {
					//返回第index后个token
					return lexer.peek(index);
				}

				Token* Pop() {
					//直接弹出一个token
					return lexer.getTok();
				}
		};

		class SyntaxScope {	//用于记录每个作用域的变量，防止变量重定义、变量未定义就使用
			public:
				//这个类要区分与vm::ObjectScope
				StringMap<void> scope;
				int isWall = 0;
				/*
				 * 这里需要详细介绍一下isWall的用法
				 * 在用户编程的过程中，可能会出现如下代码：
				 func f1 {
					 def a = 0;
					 func f2 {
						 a += 1;
					 }
				 }
				 * 容易看出，由于f1和f2是两个不同的函数，所以两个函数的作用域不能互相使用
				 * 换句话说：在f2中使用f1中的a是非法行为
				 * 但是如果采取常规的作用域分析——因为f2在f1内定义，所以f2可以使用变量a
				 * 这样就导致了分析的出错
				 * 为此，我设立了一个标志——isWall，所有函数、类的作用域的isWall都为1
				 * 其余的作用域（例如if,while等）为0
				 * 在搜寻某个标识符是否存在时，应当从作用域栈的栈顶开始从上到下搜索
				 * 直到碰到一个isWall=1的作用域为止
					 * （由于这种标志很想一堵墙，阻止了搜索，所以我取名为“墙”）
				 * 这样就防止一个函数使用另外一个函数的变量，但是并未报错
				*/

				STMException* ex;

				SyntaxScope() {}

				SyntaxScope(const SyntaxScope& s) {
					ex = s.ex;
					scope = s.scope;
				}

				SyntaxScope(STMException* e) {
					ex = e;
				}

				bool exist(Token* iden) {
					return scope.containsKey(((IdenToken*)iden)->iden);
				}

				void mark(Token* iden) {	//声明一个变量
					if(scope.containsKey(((IdenToken*)iden)->iden)) {
						THROW_S(
						    String((char*)"variable \"")
						    +((IdenToken*)iden)->iden
						    + String((char*)"\" are declared repeatedly")
						)
						return;
					}
					scope.put(((IdenToken*)iden)->iden, NULL);
				}

				void force_mark(Token* iden) {
					//无论该标识符是否被定义过，强制定义一遍
					//该函数用于弱定义
					scope.put(((IdenToken*)iden)->iden, NULL);
				}

				void destroy() {
					scope.destroy();
				}
		};

		class SourceSyntax {	//用来存储一个文件的文件名和程序
			public:
				ast::AstNode* program;
				String filename;
		};

		class Parser {
				Matcher matcher;
				//定义双目运算符的优先级表
				int bin_prio[19] = {
					1, 2, 3, 4, 5,
					6, 6,
					7, 7, 7, 7,
					8, 8,
					9, 9,
					10, 10, 10,
					-1
					//这个-1用于内部的迭代语法分析，我们称为结束符
				};
				//bin_prio[i]代表_BinaryOperatorType中
				//第i个枚举的优先级数值，优先级数值越大，则越为优先

				//定义每个优先级的运算符有哪些
				int bin_layer[10][2] {
					{TokenLogOR, TokenLogOR},
					{TokenLogAND, TokenLogAND},
					{TokenBitOR, TokenBitOR},
					{TokenBitXOR, TokenBitXOR},
					{TokenBitAND, TokenBitAND},
					{TokenEqu, TokenNotEqu},
					{TokenBig, TokenLessEqu},
					{TokenLSH, TokenRSH},
					{TokenAdd, TokenSub},
					{TokenMul, TokenMod}
				};
				/*
				 * 第i层运算符在bin_layer[i-1][0]~bin_layer[i-1][1]的范围内
				 * 例如TokenBigEqu在第7层，
				 	* 则bin_layer[6][0]<=TokenBigEqu<=bin_layer[6][1]
				 * 会发现：我并没有把每层的所有运算符都罗列出来，而是只表示了范围
				*/

				ArrayList<SyntaxScope> scopes;



			public:

				int ParsingLineNo = 1;	//当前正在分析的行号
				bool ImportFlag = false;	//表示是否支持引用代码
				STMException* ex = NULL;
				String ParsingFileName;

				FileMap filemap;
				ArrayList<SourceSyntax>* src_project;
				ArrayList<String>* ErrorMsg;

				Parser(Matcher matcher, STMException* e) {
					this->matcher = matcher;
					ex = e;
					SyntaxScope global_scope(ex);
					//压入一个空的全局作用域
					scopes.add(global_scope);
					ImportFlag = false;
				}	//这个构造函数用于兼容之前的测试样例

				Parser(
				    Matcher matcher, STMException* e,
				    SyntaxScope global_scope, String filename,
				    ArrayList<SourceSyntax>* src, FileMap map,
				    ArrayList<String>* msg, bool isSupportImport
				) {
					this->matcher = matcher;
					ex = e;
					//压入全局作用域
					scopes.add(global_scope);
					ImportFlag = isSupportImport;
					ParsingFileName = filename;
					filemap = map;
					src_project = src;
					ErrorMsg = msg;
				}

				template<class T, typename...Types>
				T* Ast(int line, Types&& ...args) {
					//这个代码比较难懂，涉及到形参模板和右值引用
					T* rst = new T(args...);
					rst->lineNo = line;
					rst->filename = ParsingFileName;
					ParsingLineNo = line;
					return rst;
				}

				Token* match(int TokType) {
					Token* rst = matcher.Match(TokType);

					CATCH {
						if(matcher.Peek(0)->type!=TokenEOF) {
							ParsingLineNo = matcher.Peek(0)->lineNo;
						}
						return NULL;
					}

					ParsingLineNo = rst->lineNo;
					return rst;
				}


				AstNode* Parse() {
					return program();
				}

				AstProgram* program() {
					ArrayList<AstNode*>* stm = new ArrayList<AstNode*>();

					while(matcher.Peek(0)->type!=TokenEOF) {
						statement(stm);
						CE
					}

					return Ast<AstProgram>(
					           1,	//整个程序的行号默认为1
					           stm
					       );
				}

				AstSFN* sfn() {
					match(TokenSFN);
					CE

					AstIdentifierName *port, *arg;

					port = IDEN();
					CE

					match(TokenCmm);

					arg = IDEN();
					CE

					match(TokenSemi);
					CE

					return Ast<AstSFN>(port->lineNo ,port, arg);
				}

				AstBlock* block() {
					//代码块

					ArrayList<AstNode*>* statements = new ArrayList<AstNode*>();

					//特判是否为单个语句的代码块
					//（即simple_block）
					if(check(TokenColon)) {
						int lineNo = _pop->lineNo;	//弹出冒号
						statement(statements);
						CE
						return Ast<AstBlock>(lineNo, statements);
					}

					//如果不是simple_block，那就是常规的代码块

					GETLN(TokenLBC);	//匹配左花括号

					while(check(TokenRBC)==false) {
						statement(statements);
						CE
					}

					_pop;	//弹出右花括号

					return Ast<AstBlock>(lineNo, statements);

				}

				void* statement(ArrayList<AstNode*>* stm) {
					//读取一条语句，并将解析后的ast加入stm当中
					//这里的返回值类型为void*，纯粹为了方便CE时return NULL;
					if(check(TokenDef)) {
						def_var(stm);
					} else if(check(TokenFunc)
					          &&matcher.Peek(1)->type==TokenIden) {
						//定义函数
						stm->add(def_func());
					} else if(check(TokenClass)
					          ||matcher.Peek(1)->type==TokenClass) {
						stm->add(def_class());
					} else if(check(TokenIf)) {
						stm->add(statement_if());
					} else if(check(TokenWhile)) {
						stm->add(statement_while());
						CE
					} else if(check(TokenFor)) {
						stm->add(statement_for());
					}  else if(check(TokenReturn)) {
						stm->add(statement_return());
					} else if(check(TokenContinue)) {
						stm->add(
						    Ast<AstContinue>(
						        _pop->lineNo
						    )
						);
						match(TokenSemi);
					} else if(check(TokenBreak)) {
						stm->add(
						    Ast<AstBreak>(
						        _pop->lineNo
						    )
						);
						match(TokenSemi);
					} else if(check(TokenSemi)) {	//空语句
						_pop;	//弹出分号
					} else if(check(TokenSFN)) {
						stm->add(sfn());
					} else if(check(TokenImport)) {
						statement_import();
					} else {
						//如果以上情况都不是，那就只有可能是表达式了
						stm->add(expression());
						CE
						match(TokenSemi);
					}
					CE

					return NULL;
				}

				AstDefVar* MakeNullDef(Token* iden) {
					//用于生成一个def_var，将iden赋为空值
					return Ast<AstDefVar>(
					           iden->lineNo,
					           Ast<AstIdentifierName>(
					               iden->lineNo,
					               ((IdenToken*)iden)->iden
					           ),
					           Ast<AstExpression>(	//右值（即null）
					               iden->lineNo,
					               Ast<AstBinary>(
					                   iden->lineNo,
					                   Ast<AstUnary>(
					                       iden->lineNo,
					                       Ast<AstNull>(
					                           iden->lineNo
					                       ),
					                       new ArrayList<AstNode*>() //null无后缀
					                   )
					               )
					           )

					       );
				}

				AstDefVar* assign_new_var() {

					Token* iden = matcher.Peek(0);

					if(iden->type!=TokenIden) {
						//变量名必须为标识符
						THROW("the name of the variable must be an identifier")
						return NULL;
					}

					scopes[scopes.size()-1].mark(iden);	//登记该变量
					_pop;		//弹出iden
					CE

					if(matcher.Peek(0)->type==TokenAssign) {
						//初始化赋值
						_pop;	//弹出等号

						AstExpression* expr = expression();
						CE

						AstDefVar* rst = Ast<AstDefVar>(
						                     iden->lineNo,
						                     Ast<AstIdentifierName>(
						                         iden->lineNo,
						                         ((IdenToken*)iden)->iden
						                     ),
						                     expr
						                 );

						return rst;
					}

					//没有初始化赋值，则赋空值

					return MakeNullDef(iden);

				}

				void* def_var(ArrayList<AstNode*>* stm) {
					match(TokenDef);	//弹出def
					CE

					stm->add(assign_new_var());
					CE

					while(check(TokenCmm)) {
						//如果变量的声明不止一个
						_pop;	//弹出逗号
						stm->add(assign_new_var());
						CE
					}

					match(TokenSemi);	//匹配分号
					CE

					return NULL;
				}


				AstDefFunc* def_func() {

					GETLN(TokenFunc)

					Token* iden_tok = match(TokenIden);

					CE

					AstIdentifierName* iden = Ast<AstIdentifierName>(
					                              iden_tok->lineNo,
					                              ((IdenToken*)iden_tok)->iden
					                          );

					scopes[scopes.size()-1].force_mark(iden_tok);	//弱定义

					//新建作用域
					pushscope(1)

					ArrayList<AstNode*>* args = new ArrayList<AstNode*>();

					if(check(TokenLBR)) {
						//有参数列表
						_pop;	//弹出左括号

						if(check(TokenIden)) {
							IdenToken* iden = (IdenToken*)_pop;
							args->add(
							    Ast<AstIdentifierName>(
							        iden->lineNo,
							        iden->iden
							    )
							);
							//在新建作用域中登记参数
							scopes[scopes.size()-1].mark(iden);
							CE
						}

						while(check(TokenCmm)) {
							//只要后面还有参数
							_pop;	//弹出逗号
							IdenToken* iden = (IdenToken*)_pop;
							args->add(
							    Ast<AstIdentifierName>(
							        iden->lineNo,
							        iden->iden
							    )
							);
							//在新建作用域中登记参数
							scopes[scopes.size()-1].mark(iden);
							CE
						}

						if(match(TokenRBR)==NULL) {
							THROW("the parentheses are not closed")
						}
					}

					CE

					AstBlock* blk = block();
					CE

					popscope;

					return Ast<AstDefFunc>(
					           lineNo,
					           iden,
					           Ast<AstAnonFunc>(
					               lineNo, args, blk
					           )
					       );

				}

				AstAnonFunc* anon_func() {

					GETLN(TokenFunc)

					//新建作用域
					pushscope(1)

					ArrayList<AstNode*>* args = new ArrayList<AstNode*>();

					if(check(TokenLBR)) {
						//有参数列表
						_pop;	//弹出左括号

						if(check(TokenIden)) {
							IdenToken* iden = (IdenToken*)_pop;
							args->add(
							    Ast<AstIdentifierName>(
							        iden->lineNo,
							        iden->iden
							    )
							);
							//在新建作用域中登记参数
							scopes[scopes.size()-1].mark(iden);
							CE
						}

						while(check(TokenCmm)) {
							//只要后面还有参数
							_pop;	//弹出逗号
							IdenToken* iden = (IdenToken*)_pop;
							args->add(
							    Ast<AstIdentifierName>(
							        iden->lineNo,
							        iden->iden
							    )
							);
							//在新建作用域中登记参数
							scopes[scopes.size()-1].mark(iden);
							CE
						}

						if(match(TokenRBR)==NULL) {
							THROW("the parentheses are not closed")
						}
					}

					CE

					AstBlock* blk = block();
					CE

					popscope;

					return Ast<AstAnonFunc>(
					           lineNo, args, blk
					       );

				}

				AstDefClass* def_class() {

					AstIdentifierName* iden;	//类的标识符
					AstIdentifierName* father = NULL;
					//父类标识符，默认没有父类

					if(matcher.Peek(0)->type==TokenIden) {
						//检查是否有父类
						father = IDEN();
						CE
					}

					GETLN(TokenClass)	//匹配class

					Token* iden_tok = match(TokenIden);
					CE
					iden = Ast<AstIdentifierName>(
					           iden_tok->lineNo,
					           ((IdenToken*)iden_tok)->iden
					       );

					scopes[scopes.size()-1].force_mark(iden_tok);	//弱定义

					match(TokenLBC);	//匹配左花括号
					CE

					//新建作用域
					pushscope(1)

					ArrayList<AstNode*>* stm = new ArrayList<AstNode*>();
					//类里的语句

					while(check(TokenRBC)==false) {
						if(check(TokenFunc)) {
							stm->add(def_func());
						} else if(check(TokenDef)) {
							def_var(stm);
						} else if(check(TokenClass)
						          ||matcher.Peek(1)->type==TokenClass) {
							stm->add(def_class());
						} else {
							THROW("only functions, classes, and variables "
							      "can be defined in a class")
							return NULL;
						}

						CE
					}

					_pop;	//弹出右花括号

					popscope;

					return Ast<AstDefClass>(
					           lineNo,
					           iden,
					           Ast<AstAnonClass>(
					               lineNo,
					               father,
					               stm
					           )
					       );

				}

				AstAnonClass* anon_class() {

					AstIdentifierName* father = NULL;
					//父类标识符，默认没有父类

					if(matcher.Peek(0)->type==TokenIden) {
						//检查是否有父类
						father = IDEN();
						CE
					}

					GETLN(TokenClass)	//匹配class

					CE

					match(TokenLBC);	//匹配左花括号
					CE

					//新建作用域
					pushscope(1)

					ArrayList<AstNode*>* stm = new ArrayList<AstNode*>();
					//类里的语句

					while(check(TokenRBC)==false) {
						if(check(TokenFunc)) {
							stm->add(def_func());
						} else if(check(TokenDef)) {
							def_var(stm);
						} else if(check(TokenClass)
						          ||matcher.Peek(1)->type==TokenClass) {
							stm->add(def_class());
						} else {
							THROW("only functions, classes, and variables "
							      "can be defined in a class")
							return NULL;
						}

						CE
					}

					_pop;	//弹出右花括号

					popscope;

					return Ast<AstAnonClass>(
					           lineNo,
					           father,
					           stm
					       );

				}

				AstIfStatement* statement_if() {
					GETLN(TokenIf);

					AstExpression* condition = expression();
					CE

					pushscope(0)

					AstBlock* block_if = block();
					CE

					if(check(TokenElse)) {
						_pop;		//弹出else
						AstBlock* block_else = block();
						CE
						popscope;
						return Ast<AstIfStatement>(
						           lineNo,
						           condition,
						           block_if,
						           block_else
						       );
					}

					popscope;

					return Ast<AstIfStatement>(
					           lineNo,
					           condition,
					           block_if
					       );

				}

				AstWhileStatement* statement_while() {
					GETLN(TokenWhile)

					AstExpression* condition = expression();
					CE

					pushscope(0)

					AstBlock* blk = block();
					CE

					popscope;

					return Ast<AstWhileStatement>(
					           lineNo,
					           condition,
					           blk
					       );
				}

				AstForStatement* statement_for() {
					GETLN(TokenFor)	//匹配for

					Token* iden_tok = match(TokenIden);
					CE
					AstIdentifierName* iden = Ast<AstIdentifierName>(
					                              iden_tok->lineNo,
					                              ((IdenToken*)iden_tok)->iden
					                          );
					//获取循环用的变量

					match(TokenIn);	//匹配in
					CE

					AstExpression* expr = expression();		//循环所需的表达式
					CE

					pushscope(0)	//新建作用域
					scopes[scopes.size()-1].mark(iden_tok);	//登记变量

					AstBlock* blk = block();	//代码块
					CE

					popscope;

					return Ast<AstForStatement>(
					           lineNo,
					           iden,
					           expr,
					           blk
					       );
				}

				AstReturnStatement* statement_return() {
					GETLN(TokenReturn);

					AstExpression* expr = expression();
					CE

					match(TokenSemi);
					CE

					return Ast<AstReturnStatement>(
					           lineNo,
					           expr
					       );
				}

				void* statement_import() {
					GETLN(TokenImport)
					ParsingLineNo = lineNo;

					if(ImportFlag==false) {
						THROW("cannot import")
						return NULL;
					}

					String import_path;	//导入路径

					Token* iden = match(TokenIden);
					CE

					import_path += ((IdenToken*)iden)->iden;

					while(check(TokenMember)) {
						import_path += String((char*)"/");	//还有一层路径
						iden = match(TokenIden);
						CE
						import_path += ((IdenToken*)iden)->iden;
					}

					import_path += String((char*)".st");

					match(TokenSemi);	//匹配分号
					CE

					/*开始分析*/
					if(filemap.exist(import_path)==true) {
						return NULL;	//已经分析过了
					}

					LineReader reader = filemap.mark(import_path);
					CE

					//进行词法分析
					lineNo = 1;
					Lexer lexer(ex);

					while(reader.isMore()) {
						String text = reader.getLine();
						CE

						int index = lexer.getLineTok(
						                lineNo, text
						            );

						CATCH {
							THROW_S(
							    String((char*)"Error: at \"")
							    + import_path
							    + String((char*)"\": ")
							    + toString(lineNo)
							    + String((char*)":")
							    + toString(index)
							    + String((char*)" : ")
							    + ex->getError()
							)
							ErrorMsg->add(ex->getError());

							ex->isError = false;
						}

						lineNo++;
					}
					
					Matcher matcher(lexer, ex);
					Parser* parser = new Parser(
					    matcher, ex, scopes[0],
					    import_path, src_project, filemap,
					    ErrorMsg, ImportFlag
					);

					ast::AstNode* node = parser->Parse();	//语法分析

					CATCH {
						THROW_S(
						    String((char*)"Syntax Error: at \"")
						    + import_path
						    + String((char*)"\": ")
						    + toString(parser->ParsingLineNo)
						    + String((char*)": ")
						    + ex->getError()
						)
						ErrorMsg->add(ex->getError());

						ex->isError = false;
					}

					SourceSyntax syntax;
					syntax.program = node;
					syntax.filename = import_path;

					src_project->add(syntax);

					reader.close();


					return NULL;

				}

				AstExpression* expression() {
					AstExpression* rst;
					AstBinary* val = binary_operator();
					CE
					if(
					    check(TokenAssign)
					    ||check(TokenAddAss)||check(TokenSubAss)
					    ||check(TokenMulAss)||check(TokenDivAss)
					    ||check(TokenModAss)
					    ||check(TokenAndAss)||check(TokenOrAss)
					    ||check(TokenXOrAss)
					    ||check(TokenLSHAss)||check(TokenRSHAss)
					) {
						//看到赋值的token
						AstLeftValue* left = left_value(val);	//解析成左值
						CE
						int ass_type = _pop->type;	//弹出赋值的token
						AstExpression* right = expression();
						CE
						return Ast<AstExpression>(
						           left->lineNo, left, ass_type, right
						       );
					} else {
						//单纯的右值表达式
						return Ast<AstExpression>(val->lineNo, val);
					}
				}

				AstLeftValue* left_value(AstBinary* val) {
					//用于将右值表达式转换为左值表达式（即AstBinary* -> AstLeftValue*）
					//如果无法转换，则报错

					/*
					 * 一个左值表达式的ast格式应该是
					 * binary_operator: -1
					 	* unary_operator: -1
						 	* quark: IDEN
							* postfix: 下标后缀或成员后缀
							* postfix: 下标后缀或成员后缀
							...
					*/

					AstNode* quark;
					ArrayList<AstNode*>* postfixs = new ArrayList<AstNode*>();

					if(val->getOperatorType()!=-1) {	//binary_operator: -1
						THROW("lvalue required as "
						      "left operand of assignment")
					}

					AstUnary* unary = (AstUnary*)val->Children()->at(0);

					if(unary->getOperatorType()!=-1) {	//unary_operator: -1
						THROW("lvalue required as "
						      "left operand of assignment")
					}

					ArrayList<AstNode*>* children = unary->Children();

					quark = children->at(0);

					//开始分析左值表达式

					if(quark->getType()!=AstIdentifierType) {
						//quark: IDEN
						THROW("lvalue required as "
						      "left operand of assignment")
					}

					CE

					for(int i=1,len=children->size(); i<len; i++) {
						//postfix: 下标后缀或成员后缀

						AstPostfix* p;		//转换成右值后缀
						p = (AstPostfix*)children->at(i);

						if(
						    p->getPostfixType()!=PostfixMemberType
						    &&p->getPostfixType()!=PostfixElementType
						) {	//如果不满足左值后缀条件
							THROW("lvalue required as "
							      "left operand of assignment")
						}


						AstLeftPostfix* tmp = Ast<AstLeftPostfix>(
						                          p->lineNo,
						                          p->getPostfixType(),
						                          p->Children()->at(0)
						                      );

						postfixs->add(tmp);


					}

					CE

					return Ast<AstLeftValue>(
					           quark->lineNo,
					           (AstIdentifierName*)quark,
					           postfixs
					       );

				}

				AstBinary* binary_operator() {
					AstNode* value = _bin_operator(1);
					CE
					return Ast<AstBinary>(value->lineNo, value);
				}

				AstNode* _bin_operator(int layer) {
					//双目运算符的主要分析算法在这里实现
					//这个函数是一个递归函数

					if(layer==11) {
						//递归边界
						return unary_operator();
					}

					AstNode* rst = _bin_operator(layer+1);
					CE

					Token* op = matcher.Peek(0);

					while (bin_layer[layer-1][0]<=op->type
					        &&op->type<=bin_layer[layer-1][1]) {
						//只有还有运算符
						_pop;	//弹出运算符
						AstNode* right = _bin_operator(layer+1);
						CE
						rst = Ast<AstBinary>(
						          op->lineNo,
						          op->type-38,
						          rst,
						          right
						      );
						//其中，op->type-38其实是将token里的运算符映射到ast中的运算符
						//例如TokenBitOR-38 = 2 = BinaryBitORType

						op = matcher.Peek(0);
					}

					return rst;
				}

				//双目运算符的迭代分析将会在后续实现

				AstUnary* unary_operator() {
					//判断是否还有前缀的单目运算符
					unary_check(TokenAdd, UnaryPositiveType)
					unary_check(TokenSub, UnaryNegative)
					unary_check(TokenBitNot, UnaryInverseType)
					unary_check(TokenLogNot, UnaryNotType)
					//如果没有，则直接返回quark { postfix }
					AstNode* q = quark();
					CE
					ArrayList<AstNode*>* postfixs = new ArrayList<AstNode*>();
					while(
					    check(TokenLBR)
					    ||check(TokenLSB)
					    ||check(TokenMember)
					) {
						//只要还有后缀运算符，就匹配
						postfixs->add(postfix());
						CE
					}
					return Ast<AstUnary>(q->lineNo, q, postfixs);
				}

				AstPostfix* postfix() {
					if(check(TokenLBR)) {
						//调用函数后缀
						AstArguments* rst = arguments();
						CE
						return Ast<AstPostfix>(
						           rst->lineNo , PostfixCallType, rst
						       );
					}
					if(check(TokenLSB)) {
						//下标后缀
						int line = _pop->lineNo;
						ParsingLineNo = line;
						AstExpression* expr = expression();
						CE
						match(TokenRSB);
						return Ast<AstPostfix>(
						           line, PostfixElementType, (AstNode*)expr
						       );
					}
					if(check(TokenMember)) {

						int line = _pop->lineNo;

						ParsingLineNo = line;

						//还要特判是不是.new
						if(check(TokenNew)) {
							match(TokenNew);
							//特性支持：如果新建对象的构造函数无参数，可以省略括号
							if(check(TokenLBR)==false) {
								return Ast<AstPostfix> (
								           line,
								           PostfixNewType,
								           (AstNode*) new AstArguments(
								               new ArrayList<AstNode*>()
								           )
								           //空的参数列表
								       );
							} else {
								//正常的.new(...)
								AstArguments* rst = arguments();
								CE
								return Ast<AstPostfix> (
								           rst->lineNo ,
								           PostfixNewType, (AstNode*)rst
								       );
							}
						}

						//正常的访问成员
						IdenToken* iden_tok = (IdenToken*)match(TokenIden);
						CTH("the member name must be an identifier")
						AstIdentifierName* iden = Ast<AstIdentifierName>(
						                              iden_tok->lineNo,
						                              iden_tok->iden
						                          );
						return Ast<AstPostfix>(line, PostfixMemberType, iden);
					}
					THROW("invalid syntax")
					return NULL;
				}

				AstArguments* arguments() {
					ArrayList<AstNode*>* exprs = new ArrayList<AstNode*>();
					int line = match(TokenLBR)->lineNo;
					CE
					if(check(TokenRBR)==false) {
						//左括号后没有紧跟着右括号，代表有传参数
						exprs->add(expression());
						CE
					}
					while(check(TokenRBR)==false) {
						//只要没有匹配到右括号
						match(TokenCmm);
						CE
						exprs->add(expression());
						CE
					}
					_pop;	//弹出右括号
					return Ast<AstArguments>(line, exprs);
				}

				AstNode* quark() {
					if(check(TokenIden)) {
						return IDEN();
					}
					if(
					    check(TokenInt)||check(TokenDouble)
					    ||check(TokenTrue)||check(TokenFalse)
					) {	//分析一个数字

						return NUM();
					}
					if(check(TokenString)) {
						return STR();
					}
					if(check(TokenNull)) {
						return _NULL();
					}
					if(check(TokenLBC)) {
						return list_literal();
					}
					if(check(TokenLSB)) {
						return array_literal();
					}
					if(check(TokenLBR)) {
						_pop;
						AstExpression* expr = expression();
						CE
						match(TokenRBR);
						CTH("the parentheses are not closed")
						return expr;
					}
					if(check(TokenFunc)) {
						return anon_func();
					}
					if(check(TokenClass)
					        ||matcher.Peek(0)->type==TokenClass) {
						return anon_class();
					}
					CE
					THROW("invalid syntax")
					return NULL;
				}

				//注意：这里的IDEN()只有在匹配变量名的时候能使用
				//如果是匹配类成员名等，请自行建立AstIdentifierName节点
				AstIdentifierName* IDEN() {
					//分析一个标识符
					IdenToken* tok = (IdenToken*)match(TokenIden);

					//分析作用域
					bool isIdenExist = false;	//标识符是否被定义过

					for(int i=scopes.size()-1; i>=0; i--) {
						if(scopes[i].exist(tok)==true) {
							isIdenExist = true;
							break;
						}
						if(scopes[i].isWall==1) {
							break;	//碰到“墙”，终止搜索
						}
					}

					if(scopes[0].exist(tok)==true) {
						isIdenExist = true;	//全局作用域也要检查
					}

					if(isIdenExist==false) {
						//未声明的标识符
						THROW_S(
						    String((char*)"undefined variable: \"")
						    + tok->iden
						    + String((char*)"\"")
						)

						return NULL;
					}

					return Ast<AstIdentifierName>(tok->lineNo, tok->iden);
				}

				AstNumber* NUM() {
					if(check(TokenInt)) {
						IntToken* tok = (IntToken*)_pop;
						ParsingLineNo = tok->lineNo;
						return Ast<AstIntNumber>(tok->lineNo, tok->val);
					} else if(check(TokenDouble)) {
						DoubleToken* tok = (DoubleToken*)_pop;
						ParsingLineNo = tok->lineNo;
						return Ast<AstDoubleNumber>(tok->lineNo, tok->val);
					} else if(check(TokenTrue)) { //布尔值在语法分析时会被转换成1和0
						ParsingLineNo = matcher.Peek(0)->lineNo;
						return Ast<AstIntNumber>(_pop->lineNo, 1);
					} else if(check(TokenFalse)) {
						ParsingLineNo = matcher.Peek(0)->lineNo;
						return Ast<AstIntNumber>(_pop->lineNo, 0);
					} else {
						THROW("invalid syntax")
					}
					return NULL;
				}

				AstString* STR() {
					StringToken* tok = (StringToken*)match(TokenString);
					return Ast<AstString>(tok->lineNo, tok->val);
				}

				AstNull* _NULL() {
					Token* t = match(TokenNull);
					CE
					return Ast<AstNull>(
					           t->lineNo
					       );
				}

				AstArrayLiteral* array_literal() {
					Token* t = match(TokenLSB);
					AstArrayLiteral* rst = Ast<AstArrayLiteral>(
					                           t->lineNo,
					                           expression()
					                       );
					match(TokenRSB);
					CTH("the square parentheses are not closed")
					return rst;
				}

				AstListLiteral* list_literal() {
					Token* t = match(TokenLBC);
					ArrayList<AstNode*>* exprs = new ArrayList<AstNode*>();

					if(check(TokenRBC)) {
						//如果列表为空
						_pop;
						return Ast<AstListLiteral>(t->lineNo, exprs);
					}

					exprs->add((AstNode*)expression());
					CE
					while(check(TokenCmm)) {
						_pop;
						exprs->add((AstNode*)expression());
						CE
					}

					AstListLiteral* rst = Ast<AstListLiteral>(t->lineNo, exprs);

					match(TokenRBC);
					CTH("the parentheses are not closed")
					return rst;
				}
		};
	}
}

/*

实例：

class c {
	func helloworld(this) {
		print("Hello world!");
	}
}

def main = func {
	def cls = c.new;
	cls.helloworld();
}();

类 输出类 {
	函数 你好世界（后台） {
		输出（“Hello world!”）；
	}
}

设 主 = 函数() {
	设 类对象 = 输出类——新建；
	类对象——你好世界();
}

类 输出类 {
	函数 你好世界（后台） {
		输出（“Hello world!”）；
	}
}

设 主 = 函数 {
	设 类对象 = 输出类——新建；
	类对象——你好世界（）；
}（）；

*/

#undef check
#undef pop
#undef CE

#undef unary_check
#undef GETLN
#undef pushscope

#endif