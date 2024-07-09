// 为了防止互相引用的文件出现“未命名变量”的情况
// 我们借鉴了C++的前置定义
// 利用def先定义函数名，接着进行具体声明

//数学库定义
def abs;
def all;
def range;
def range_ab;
def int;
def len;

//IO库定义
def puts;
def print;
def println;
def input;

//平台库定义
def System;

//字符串库定义
def str;

//外部调用库定义
def native;

//文件处理库定义
def File;
def open;