/*
	Name: Stamon.cpp
	Copyright: Apache 2.0
	Author: CLimber-Rong
	Date: 24/12/23 11:23
	Description: Stamon头文件
*/

#ifndef STAMON_HPP
#define STAMON_HPP

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
#include"AstIR.cpp"
#include"Compiler.hpp"
#include"AstRunner.cpp"
#include"SFN.cpp"
#include"Exception.hpp"
#include"BinaryWriter.hpp"
#include"BinaryReader.hpp"

#define WRITE(b) \
	writer.write(b);\
	CATCH {\
		ErrorMsg->add(ex->getError());\
		return;\
	}

#define WRITE_I(n) \
	if(true) {\
		int tmp = n;\
		WRITE(tmp>>24)\
		WRITE( (tmp>>16) & 0xff)\
		WRITE( (tmp>>8) & 0xff)\
		WRITE(tmp & 0xff)\
		CATCH {\
			ErrorMsg->add(ex->getError());\
			return;\
		}\
	}

#define STAMON_VER_X 2
#define STAMON_VER_Y 4
#define STAMON_VER_Z 3

namespace stamon {
	using namespace stamon::ir;
	using namespace stamon::datatype;
	using namespace stamon::c;
	using namespace stamon::vm;
	using namespace stamon::sfn;

	class Stamon {
			template<typename T>
			void SpliceArrayList(ArrayList<T>* dst, ArrayList<T>* src) {
				for(int i=0,len=src->size(); i<len; i++) {
					dst->add(src->at(i));
				}
			}
		public:
			STMException* ex;
			ArrayList<String>* ErrorMsg;

			int VerX, VerY, VerZ;   //这三个变量代表版本为X.Y.Z

			Stamon() {}

			void Init() {
				ex = new STMException();
				ErrorMsg = new ArrayList<String>();
				VerX = STAMON_VER_X;
				VerY = STAMON_VER_Y;
				VerZ = STAMON_VER_Z;
			}

			void compile(
			    String src, String dst, bool isSupportImport
			) {
				Compiler compiler(ex);

				compiler.compile(src, isSupportImport);	//开始编译

				if(compiler.ErrorMsg->empty()==false) {
					ErrorMsg = compiler.ErrorMsg;
					return;
				}

				//将编译结果整合成一个AST

				ArrayList<AstNode*>* program = new ArrayList<AstNode*>();

				for(int i=0,len=compiler.src->size(); i<len; i++) {
					SpliceArrayList(
					    program, compiler.src->at(i).program->Children()
					);
				}

				AstNode* node = new AstProgram(program);

				//编译为IR

				AstIRGenerator generator;

				ArrayList<AstIR> ir_list = generator.gen(node);

				//开始写入

				ArrayList<DataType*> ir_tableconst = generator.tableConst;

				BinaryWriter writer(ex, dst);

				CATCH {
					ErrorMsg->add(ex->getError());
					return;
				}

				//写入魔数

				WRITE(0xAB)
				WRITE(0xDB)

				//写入版本

				WRITE(VerX)
				WRITE((VerY<<4) + (VerZ & 0x0f))

				//写入常量表

				WRITE_I(ir_tableconst.size())

				for(int i=0,len=ir_tableconst.size(); i<len; i++) {

					WRITE((char)ir_tableconst[i]->getType())

					if(ir_tableconst[i]->getType()==IntegerTypeID) {
						int val = ((IntegerType*)ir_tableconst[i])
						          ->getVal();

						WRITE_I(val)
					}

					if(ir_tableconst[i]->getType()==FloatTypeID) {
						float val = ((FloatType*)ir_tableconst[i])
						            ->getVal();
						//逐个字节写入
						WRITE( ((char*)&val)[0] )
						WRITE( ((char*)&val)[1] )
						WRITE( ((char*)&val)[2] )
						WRITE( ((char*)&val)[3] )
					}

					if(ir_tableconst[i]->getType()==DoubleTypeID) {
						float val = ((DoubleType*)ir_tableconst[i])
						            ->getVal();
						//逐个字节写入
						WRITE( ((char*)&val)[0] )
						WRITE( ((char*)&val)[1] )
						WRITE( ((char*)&val)[2] )
						WRITE( ((char*)&val)[3] )
						WRITE( ((char*)&val)[4] )
						WRITE( ((char*)&val)[5] )
						WRITE( ((char*)&val)[6] )
						WRITE( ((char*)&val)[7] )
					}

					if(ir_tableconst[i]->getType()==StringTypeID) {
						String s = ((StringType*)ir_tableconst[i])
						           ->getVal();

						WRITE_I(s.length());

						for(int i=0,len=s.length(); i<len; i++) {
							WRITE(s[i])
						}
					}

					if(ir_tableconst[i]->getType()==IdenConstTypeID) {
						String s = ((IdenConstType*)ir_tableconst[i])
						           ->getVal();

						WRITE_I(s.length());

						for(int i=0,len=s.length(); i<len; i++) {
							WRITE(s[i])
						}
					}

				}

				//最后写入IR

				int lineNo = -1;			//当前正在写入的行号
				String filename((char*)"");	//当前正在写入的文件名

				for(int i=0,len=ir_list.size(); i<len; i++) {

					if(lineNo!=ir_list[i].lineNo
					        &&ir_list[i].type!=-1) {

						//注意：结束符不用在意行号和文件名
						//这里要特判结束符（即ir_list[i].type!=-1）

						//如果行号没更新，更新行号并且将当前行号写入
						lineNo = ir_list[i].lineNo;

						WRITE_I(-2)		//-2代表更新行号
						WRITE_I(lineNo)
					}

					if(filename.equals(ir_list[i].filename) == false
					        &&ir_list[i].type!=-1) {
						//检查文件名，原理同上
						filename = ir_list[i].filename;

						WRITE_I(-3)		//-2代表更新文件
						WRITE_I(filename.length())

						for(int i=0,len=filename.length(); i<len; i++) {
							WRITE(filename[i])
						}
					}

					WRITE_I(ir_list[i].type)
					WRITE_I(ir_list[i].data)
				}

				writer.close();

				return;
			}

			void run(String src, bool isGC, int MemLimit) {

				//实现流程：文件读取器->字节码读取器->IR读取器->虚拟机
				printf("FUCK RUNNER\n");
				ArrayList<AstIR> ir_list;

				BinaryReader reader(ex, src);	//打开文件

				STVCReader ir_reader(reader.read(), reader.size, ex);
				//初始化字节码读取器
				printf("FUCK CATCHER\n");
				CATCH {
					ErrorMsg->add(ex->getError());
					return;
				}
				printf("FUCK READER\n");
				if(ir_reader.ReadHeader()==false) {
					//读取文件头
					printf("FUCKING READER\n");
					ErrorMsg->add(ex->getError());
					return;
				}
				printf("FUCK IRER\n");
				ir_list = ir_reader.ReadIR();
				printf("FUCK IR LISTER\n");
				CATCH {
					ErrorMsg->add(ex->getError());
					return;
				}
				printf("FUCK CLOSER\n");
				reader.close();	//关闭文件

				printf("FUCK LINUXER\n");
				//复制版本号
				VerX = ir_reader.VerX;
				VerY = ir_reader.VerY;
				VerZ = ir_reader.VerZ;

				AstIRGenerator generator;	//初始化IR读取器

				generator.tableConst = ir_reader.tableConst;
				//复制常量表到IR读取器

				AstRunner runner;

				AstNode* running_node = generator.read(ir_list);

				printf("EXECUTE\n");
				runner.excute(
				    running_node, isGC, MemLimit, generator.tableConst,
				    ArrayList<String>(), ex
				);

				CATCH {
					ErrorMsg->add(ex->getError());
					return;
				}

				return;
			}

			ArrayList<String>* getErrorMessages() {
				return ErrorMsg;
			}

	};
}

#endif