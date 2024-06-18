/*
	Name: Lexer.cpp
	Copyright: Apache2.0
	Author: CLimber-Rong
	Date: 24/12/23 12:00
	Description: 词法分析器
*/

#ifndef LEXER_CPP
#define LEXER_CPP

#include"String.hpp"
#include"ArrayList.hpp"
#include"stmlib.hpp"
#include"Exception.hpp"


#define CHECK_KEYWORD(keyword, TokType) \
	if(iden==String((char*)keyword)) {\
		st = ed;\
		Token* rst = new Token(line, TokType);\
		tokens.add(rst);\
		continue;\
	}
//为了方便判断关键字，我编写了这个宏

#define STR_EX(len) \
	if(text_len-ed<(len-1)) {\
		THROW("the string was entered incorrectly")\
		return st;\
	}
//为了方便判断字符串token是否错误，我编写了这个宏（全名应该叫做CHECK_STRING_EXCEPTION）
//这个宏用于检查文本后还有是否还有len个字符（不包括当前正在扫描的字符）

#define CHECK_OPERATOR(op, TokType) \
	if(text[ed]==op) {\
		ed++;\
		st = ed;\
		Token* rst = new Token(line, TokType);\
		tokens.add(rst);\
		continue;\
	}
//这个宏用于匹配单个字符的运算符，其中op应是char类型

#define CHECK_LONG_OPERATOR(op, TokType) \
	if(text_len-ed>1) {\
		if(text[ed]==op[0]&&text[ed+1]==op[1]) {\
			ed += 2;\
			st = ed;\
			Token* rst = new Token(line, TokType);\
			tokens.add(rst);\
			continue;\
		}\
	}

//这个宏用于匹配两个字符的运算符，其中op应是char*类型

#define CHECK_LONG_LONG_OPERATOR(op, TokType) \
	if(text_len-ed>2) {\
		if(text[ed]==op[0]&&text[ed+1]==op[1]&&text[ed+2]==op[2]) {\
			ed += 3;\
			st = ed;\
			Token* rst = new Token(line, TokType);\
			tokens.add(rst);\
			continue;\
		}\
	}
//这个宏用于匹配三个字符的运算符，其中op应是char*类型

#define CHECK_ESCAPE_CHAR(charactor, str) \
	if(tmp[i]==charactor) {\
		val += String((char*)str);\
		i++;\
	} else

//这个宏用于StringToken当中，用于简便的判断转义字符

//以上定义的宏，都只能在该文件中使用


namespace stamon {
	namespace c {   //编译器命名空间

		enum TOKEN_TYPE {
		    TokenClass = 0,
		    TokenDef,
		    TokenExtends,
		    TokenFunc,
		    TokenBreak,
		    TokenContinue,
		    TokenIf,
		    TokenElse,
		    TokenWhile,
		    TokenFor,
		    TokenIn,
		    TokenReturn,
		    TokenSFN,
		    TokenNew,
		    TokenNull,      //空值
		    TokenImport,	//导入
		    KEYWORDS_MAX,   //关键词个数
		    TokenAssign,//赋值
		    TokenSemi,  //分号
		    TokenLBC,   //左花括号
		    TokenRBC,   //右花括号
		    TokenLBR,   //左括号
		    TokenRBR,   //右括号
		    TokenLSB,   //左方括号
		    TokenRSB,   //右方括号
		    TokenCmm,   //逗号
		    TokenColon,	//冒号
		    TokenMember,//小数点
		    TokenAddAss,    //加等于
		    TokenSubAss,
		    TokenMulAss,
		    TokenDivAss,
		    TokenModAss,
		    TokenAndAss,
		    TokenXOrAss,
		    TokenOrAss,
		    TokenLSHAss,
		    TokenRSHAss,
		    TokenLogOR, //逻辑运算符
		    TokenLogAND,
		    TokenBitOR, //位运算符
		    TokenBitXOR,
		    TokenBitAND,
		    TokenEqu,
		    TokenNotEqu,
		    TokenBig,
		    TokenLess,
		    TokenBigEqu,
		    TokenLessEqu,
		    TokenLSH,   //左移
		    TokenRSH,   //右移
		    TokenAdd,
		    TokenSub,
		    TokenMul,
		    TokenDiv,
		    TokenMod,
		    TokenLogNot,    //逻辑非
		    TokenBitNot,    //按位取反
		    TokenIden,      //标识符
		    TokenInt,       //整数
		    TokenDouble,    //浮点数
		    TokenString,    //字符串
		    TokenTrue,		//布尔真值
		    TokenFalse,		//布尔假值
		    TokenEOF
		};

		class Token {
			public:
				Token(int line, int tok_type) {
					lineNo = line;
					type = tok_type;
				}
				int type;
				int lineNo;
		};

		class StringToken : public Token {
			public:
				String content;
				//这个字符串存储着token文本
				//（token文本并没有去掉字符串前后的引号，以及转义字符）
				String val;
				//这个字符串由content处理而来
				StringToken(int line, const String& s)
					: Token(line, TokenString) {

					content = s;

					//开始处理content，并存储到s
					String tmp = content.substring(1,content.length()-1);
					//去除前后的引号
					int i=0;
					while(i<tmp.length()) {
						if(tmp[i]=='\\') {
							//碰到转义字符
							i++;
							CHECK_ESCAPE_CHAR('\"', "\"")
							CHECK_ESCAPE_CHAR('\\', "\\")
							CHECK_ESCAPE_CHAR('0', "\0")
							CHECK_ESCAPE_CHAR('n', "\n")
							CHECK_ESCAPE_CHAR('t', "\t")
							if(tmp[i]=='x') {
								i++;
								String data = tmp.substring(i, i+2);
								char* c = new char[1];
								*c = data.toIntX();
								val += String(c);
								delete[] c;
								i+=2;
							}
						} else {
							val += tmp.substring(i, i+1);	//把tmp[i]拼到val里
							i++;
						}
					}
				}
		};

		class IdenToken : public Token {
			public:
				String iden;
				IdenToken(int line, const String& name)
					: Token(line, TokenIden) {
					iden = name;
				}
		};

		class IntToken : public Token {
			public:
				int val;
				IntToken(int line, int value) : Token(line, TokenInt) {
					val = value;
				}
		};

		class DoubleToken : public Token {
			public:
				double val;
				DoubleToken(int line, double value) : Token(line, TokenDouble) {
					val = value;
				}
		};

		Token TokEOF(-1, TokenEOF);

		class Lexer {
				/*
				 * 词法分析器有三个最主要的函数：getTok，peek和getLineTok
				 * 首先，你需要逐行的读取文本，并调用getLineTok(行号, 文本)
				 	* 这个函数可以将文本分析成一系列token，并把这些token存入缓冲区
				 * 在所有的文本被逐行读取并分析成token之后，就可以正常使用getTok和peek了
				 * getTok()会从缓冲区里读取一个token，并把这个token从缓冲区里移除
					*（可以简单理解为弹出缓冲区的第一个元素）
				 * peek(index)会获取第index个token，但是不会从缓冲区里移除它
				 	*（可以简单理解为读取操作）
				*/

				ArrayList<Token*> tokens;	//缓存token用

			public:

				STMException* ex;

				Lexer() {}

				Lexer(STMException* e) {
					ex = e;
				}

				Token* getTok() {
					//读取一个Token
					if(tokens.empty()==true) {
						return &TokEOF;
					}

					Token* rst = tokens.at(0);
					tokens.erase(0);

					return rst;
				}

				Token* peek(int index) {
					//查看第index个Token
					if(tokens.size()<=index) {
						return &TokEOF;
						//没有这么多Token了
					}

					Token* rst = tokens[index];

					return rst;
				}

				int getLineTok(int line, String text) {
					//分析一行的token，line是行号，text是文本（不包含换行符）
					//分析后的token添加在tokens的末尾
					//返回解析到的位置，如果返回值是text.length()就说明解析到末尾，即解析成功
					//否则说明解析失败
					int text_len = text.length();

					int st = 0;
					while(st<text_len) {

						int ed = st;

						//先过滤空格
						while(
						    ed<text_len
						    &&(text[ed]==' '||text[ed]=='\t'
						       ||text[ed]=='\n'||text[ed]=='\r')
						) {
							ed++;
						}

						st = ed;

						if(!(st<text_len)) {
							//如果读完空格后就已经整行读完了
							return text_len; //直接正常退出即可
						}

						if(
						    text_len-st-1>=2
						    &&text[ed]=='/'
						    &&text[ed+1]=='/'
						) {
							//碰到注释，返回
							ed += 2;
							return text_len;	//直接正常退出即可
						}

						if('0'<=text[ed]&&text[ed]<='9') {
							bool isInt = true;  //该token是否是整数
							ed++;
							while(ed<text_len
							        &&'0'<=text[ed]
							        &&text[ed]<='9') {
								ed++;
							} //分析整数

							if(text[ed]=='.') {
								isInt = false;  //小数token
								ed++;
								while(ed<text_len
								        &&'0'<=text[ed]
								        &&text[ed]<='9') {
									ed++;
								}
							} //分析小数
							if(isInt==true) {
								int value = text.substring(st, ed).toInt();
								IntToken* rst = new IntToken(line, value);
								tokens.add((Token*)rst);
							} else {
								double value = text
								               .substring(st, ed)
								               .toDouble();
								DoubleToken* rst = new DoubleToken(line, value);
								tokens.add((Token*)rst);
							}
							st = ed;
							continue;
						} else if(
						    ('A'<=text[ed]&&text[ed]<='Z')
						    ||('a'<=text[ed]&&text[ed]<='z')
						    ||(text[ed]=='_')
						) {     //标识符
							ed++;
							while(
							    ed<text_len
							    &&(
							        ('A'<=text[ed]&&text[ed]<='Z')
							        ||('a'<=text[ed]&&text[ed]<='z')
							        ||('0'<=text[ed]&&text[ed]<='9')
							        ||(text[ed]=='_')
							    )
							) {
								ed++;
							}
							String iden = text.substring(st, ed);

							//判断关键字
							CHECK_KEYWORD("class", TokenClass)
							CHECK_KEYWORD("def", TokenDef)
							CHECK_KEYWORD("extends", TokenExtends)
							CHECK_KEYWORD("func", TokenFunc)
							CHECK_KEYWORD("break", TokenBreak)
							CHECK_KEYWORD("continue", TokenContinue);
							CHECK_KEYWORD("if", TokenIf)
							CHECK_KEYWORD("else", TokenElse)
							CHECK_KEYWORD("while", TokenWhile)
							CHECK_KEYWORD("for", TokenFor)
							CHECK_KEYWORD("in", TokenIn)
							CHECK_KEYWORD("return", TokenReturn)
							CHECK_KEYWORD("sfn", TokenSFN)
							CHECK_KEYWORD("new", TokenNew)
							CHECK_KEYWORD("null", TokenNull)
							CHECK_KEYWORD("import", TokenImport)
							CHECK_KEYWORD("true", TokenTrue)
							CHECK_KEYWORD("false", TokenFalse)
							//都不是关键字的话，那就是正常的标识符

							st = ed;
							IdenToken* rst = new IdenToken(line, iden);
							tokens.add((Token*)rst);
							continue;
						} else if(text[ed]=='\"') {
							//读取字符串
							bool is_str_closed = false;
							//用于标记字符串的是否有用'\"'结尾
							//因为循环的退出可能并不是因为读到'\"'，而是文本读完了
							ed++;
							while(ed<text_len&&is_str_closed==false) {
								if(text[ed]=='\"') {
									//字符串结束
									ed++;
									is_str_closed = true;

									String s = text.substring(st,ed);
									StringToken* rst = new StringToken(line, s);
									tokens.add((Token*)rst);
									st = ed;
									continue;
								} else if(text[ed]=='\\') {
									//碰到转义字符
									STR_EX(2)	//文本至少还得剩两个字符
									ed++;
									if(
									    text[ed]=='\"'
									    ||text[ed]=='\\'
									    ||text[ed]=='0'
									    ||text[ed]=='n'
									    ||text[ed]=='t'
									) {
										ed++;
									} else if(text[ed]=='x') {
										STR_EX(3)	//文本至少还得剩三个字符

										if(
										    (
										        ('0'<=text[ed+1]
										         &&text[ed+1]<='9')
										        ||('a'<=text[ed+1]
										           &&text[ed+1]<='f')
										        ||('A'<=text[ed+1]
										           &&text[ed+1]<='F')
										    )
										    &&(
										        ('0'<=text[ed+2]
										         &&text[ed+1]<='9')
										        ||('a'<=text[ed+2]
										           &&text[ed+1]<='f')
										        ||('A'<=text[ed+2]
										           &&text[ed+1]<='F')
										    )
										) {
											//用于判断\xDD转义字符中，DD是否符合标准
											ed += 2;
										} else {
											STR_EX(text_len)
											//由于字符串永远无法剩下text_len个字符
											//所以异常必然会被触发
											//所以STR_EX(text_len)的目的，
											//就是直接抛出异常
											//这里运用了取巧的设计
										}

									}
								} else {
									//正常字符
									ed++;
								}
							}
						} else {
							CHECK_LONG_LONG_OPERATOR("<<=", TokenLSHAss)
							CHECK_LONG_LONG_OPERATOR(">>=", TokenRSHAss)

							CHECK_LONG_OPERATOR("+=", TokenAddAss)
							CHECK_LONG_OPERATOR("-=", TokenSubAss)
							CHECK_LONG_OPERATOR("*=", TokenMulAss)
							CHECK_LONG_OPERATOR("/=", TokenDivAss)
							CHECK_LONG_OPERATOR("%=", TokenModAss)
							CHECK_LONG_OPERATOR("&=", TokenAndAss)
							CHECK_LONG_OPERATOR("^=", TokenXOrAss)
							CHECK_LONG_OPERATOR("|=", TokenOrAss)
							CHECK_LONG_OPERATOR("||", TokenLogOR)
							CHECK_LONG_OPERATOR("&&", TokenLogAND)
							CHECK_LONG_OPERATOR("==", TokenEqu)
							CHECK_LONG_OPERATOR("!=", TokenNotEqu)
							CHECK_LONG_OPERATOR(">=", TokenBigEqu)
							CHECK_LONG_OPERATOR("<=", TokenLessEqu)
							CHECK_LONG_OPERATOR("<<", TokenLSH)
							CHECK_LONG_OPERATOR(">>", TokenRSH)

							CHECK_OPERATOR('=', TokenAssign)
							CHECK_OPERATOR(';', TokenSemi)
							CHECK_OPERATOR('{', TokenLBC)
							CHECK_OPERATOR('}', TokenRBC)
							CHECK_OPERATOR('(', TokenLBR)
							CHECK_OPERATOR(')', TokenRBR)
							CHECK_OPERATOR('[', TokenLSB)
							CHECK_OPERATOR(']', TokenRSB)
							CHECK_OPERATOR(',', TokenCmm)
							CHECK_OPERATOR(':', TokenColon)
							CHECK_OPERATOR('.', TokenMember)
							CHECK_OPERATOR('|', TokenBitOR)
							CHECK_OPERATOR('^', TokenBitXOR)
							CHECK_OPERATOR('&', TokenBitAND)
							CHECK_OPERATOR('>', TokenBig)
							CHECK_OPERATOR('<', TokenLess)
							CHECK_OPERATOR('+', TokenAdd)
							CHECK_OPERATOR('-', TokenSub)
							CHECK_OPERATOR('*', TokenMul)
							CHECK_OPERATOR('/', TokenDiv)
							CHECK_OPERATOR('%', TokenMod)
							CHECK_OPERATOR('!', TokenLogNot)
							CHECK_OPERATOR('~', TokenBitNot)
							//执行到这里，说明碰到了未知字符
							THROW_S(
							    String((char*)"unknown token: \'")
							    + text.substring(ed, ed+1)
							    + String((char*)"\'")
							)
							return st;
						}
					}
					return st;	//到这里说明一切正常
				}


		};
	}
}

#undef CHECK_KEYWORD
#undef STR_EX
#undef CHECK_OPERATOR
#undef CHECK_LONG_OPERATOR
#undef CHECK_LONG_LONG_OPERATOR

#endif