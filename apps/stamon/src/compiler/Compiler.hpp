/*
	Name: Compiler.hpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 22/02/24 12:31
	Description: 编译器头文件
*/

#ifndef COMPILER_HPP
#define COMPILER_HPP

#include"Parser.cpp"

namespace stamon {
	namespace c {
		class Compiler {
			public:
				FileMap filemap;
				SyntaxScope global_scope;
				ArrayList<SourceSyntax>* src;
				STMException* ex;
				ArrayList<String>* ErrorMsg;
				bool ImportFlag = false;

				Compiler() {}

				Compiler(Compiler& right) {
					filemap = right.filemap;
					global_scope = right.global_scope;
					src = right.src;
					ex = right.ex;
					ErrorMsg = right.ErrorMsg;
					ImportFlag = right.ImportFlag;
				}

				Compiler(STMException* e) : filemap(e), global_scope(e) {
					src = new ArrayList<SourceSyntax>();
					ex = e;
					ErrorMsg = new ArrayList<String>();
				}


				void compile(String filename, bool isSupportImport) {
					ImportFlag = isSupportImport;

					LineReader reader = filemap.mark(filename);

					CATCH {
						return;
					}

					int lineNo = 1;
					Lexer lexer(ex);

					while(reader.isMore()) {
						String text = reader.getLine();

						CATCH {
							return;
						}

						int index = lexer.getLineTok(
						                lineNo, text
						            );

						CATCH {
							THROW_S(
							    String((char*)"Error: at \"")
							    + filename
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
					    matcher, ex, global_scope,
					    filename, src, filemap,
					    ErrorMsg, ImportFlag
					);

					ast::AstNode* node = parser->Parse();	//语法分析

					CATCH {
						THROW_S(
						    String((char*)"Syntax Error: at \"")
						    + filename
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
					syntax.filename = filename;

					src->add(syntax);

					reader.close();
				}
		};
	}
}

#endif
