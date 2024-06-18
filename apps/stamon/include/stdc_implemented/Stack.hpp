/*
	Name: Stack.cpp
	Copyright: Apache 2.0
	Author: 瞿相荣 
	Date: 17/01/23 18:06
	Description: 栈库，基于stack.h
*/
#ifndef STACK_CPP
#define STACK_CPP

#include"stack.h"

template <typename T>
class Stack {
	STACK* stack;
	public:
		Stack();								//构造函数 
		int clear();							//清空
		int destroy();							//销毁
		int empty();							//是否为空
		int size();								//元素个数
		T* peek();							//获取栈顶元素
		int push(T* data); 					//入栈
		T* pop();							//出栈
		int traverse(STACK_VISIT visit); 		//从栈底到栈顶依次遍历，具体操作见stack.h 
};

template <typename T>
Stack<T>::Stack()
{
	stack = InitStack();
}

template <typename T>
int Stack<T>::clear()
{
	return ClearStack(stack);
}

template <typename T>
int Stack<T>::destroy()
{
	return DestroyStack(stack);
}

template <typename T>
int Stack<T>::empty()
{
	return StackEmpty(stack);
}

template <typename T>
int Stack<T>::size()
{
	return StackLength(stack);
}

template <typename T>
T* Stack<T>::peek()
{
	return (T*)GetTop(stack);
}

template <typename T>
int Stack<T>::push(T* data)
{
	return PushStack(stack,(void*)data);
}

template <typename T>
T* Stack<T>::pop()
{
	return (T*)PopStack(stack);
}

template <typename T>
int Stack<T>::traverse(STACK_VISIT visit)
{
	return StackTraverse(stack,visit);
}

#endif
