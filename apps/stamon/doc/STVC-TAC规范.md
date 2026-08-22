<!--
 * @Name: 
 * @Copyright: 
 * @Author: 
 * @Date: 25/06/24 12:18
 * @Description: 
-->
# STVC-TAC规范

### 前言

##### 什么是STVC-TAC？

``STVC-TAC``（以下简称``STVC``）是一种以三地址码为基础的平面字节码规范。

> ``STVC-TAC``区别于``AST-IR``，前者的字节码是线性且平面的，而后者则是递归且树状的。

##### 为什么要设计STVC-TAC？

比起形似AST的``AST-IR``，``STVC``更容易优化，且执行速度相对更快。

### STVC文件结构

与AST-IR相似的，AST-IR具有：

1. 魔数（0xABDB）
2. 常量表
3. 代码

之前似乎未曾说明常量表的格式，故接下来详述。

### 常量表格式

一个常量表由常量表长度（占4字节）和若干个常量组成，其中常量又由常量类型（占1字节）和常量值组成：

其中常量值有以下类型：

* 整数类型：占4字节，即数值
* 单精度浮点类型：占4字节，即数值
* 双精度浮点类型：占8字节，即数值
* 字符串类型：占4+l字节，其中前4字节记录字符串长度（即l，长度按字节计），后l字节为字符串值
* 标识符类型：占4+l字节，其中前4字节记录标识符长度（即l，长度按字节计），后l字节为标识符名

将所有涉及到的数据和标识符存入常量表，这样在字节码代码中，若涉及到某数值或标识符，只需指定一个下标，虚拟机就能通过下标在常量表中查找出对应的数值或标识符。这么做极大的减少了冗余数据的存储，减小了程序体积。

**注意：按照规定，常量表的第一条常量必须是一个名为``__init__``的标识符**

### 代码格式

##### 标识符

标识符有三个种类：用户标识符，临时标识符和内部标识符。

用户标识符一般为用户自定义的标识符，其格式与C标识符格式相同。

临时标识符则是表达式计算过程当中会用到的标识符，此类标识符的格式为``.XXX``，其中“XXX”通常为数字。

内部标识符则是用于匿名类、匿名函数的声明，此类标识符的格式为``#XXX``，其中“XXX”通常为数字

##### 声明函数

```
function identifier: arg1 arg2 arg3 ...
...some codes...
end
```

其中identifier为函数名。arg1、arg2、arg3等为函数的参数名。“...some codes...”为函数体代码

函数内部不能嵌套声明函数或类。

##### 声明类

```
class identifier: member1 member2 member3 ...
...some codes...
end
```

其中identifier为类名。member1、member2、member3等为类成员名。“...some codes...”为类初始化赋值代码。

在初始化类对象时，会先执行初始化赋值代码，再调用构造函数。

类内部不能嵌套声明函数或类。

##### 三地址语句

1. ``x = y op z``

将y和z进行运算（运算符为op）之后的值存入x

2. ``x = y op``

将y进行单目运算（运算符为op）之后的值存入x。如果op为``=``，则不做任何运算，这样做能达成“将y直接赋值于x”的效果

##### 流程控制语句

1. ``goto addr``

无条件跳转至相对addr行指令所在处。若addr<0，向上跳转，否则向下跳转。

2. ``if condition => addr``
   
如果condition不为``null``或``0``则跳转至第addr行指令所在处。若addr<0，向上跳转，否则向下跳转。

3. ``call result function: arg1 arg2 ...``

调用function值。参数为arg1、arg2等标识符或值。返回值存入result当中。

4. ``return value``

返回value值

##### 特别的...

1. ``new object source arg1 arg2 ...``

将source标识符作为类，新建对象，构造参数为arg1、arg2等，新建后的对象值存入object标识符

1. ``sfn port arg``

设置SFN，port为端口号标识符。arg参数标识符。

SFN的介绍见``编译器开发文档.md``

1. ``list identifier element1 element2...``

将element1、element2...作为元素，组合成数列，并存入identifier标识符中s

4. ``pushscope``

压入一个作用域，用于跳转指令

5. ``popscope``

弹出一个作用域，用于跳转指令

6. ``free identifier``

如果identifier所存储的是字面量值（如整型），则释放。该指令和C语言的register关键字类似，**是否释放取决于虚拟机状态。**该指令通常用于释放临时标识符。

##### 附表

双目运算符有以下种类：

* add：加
* sub：减
* mul：乘
* div：除
* mod：取余
* lsh：左移
* rsh：右移
* less：小于
* lequ：小等于
* big：大于
* bequ：大等于
* equ：判等
* iequ：判不等
* band：按位与
* bxor：按位异或
* bor：按位或
* land：逻辑与
* lor：逻辑或
* member：取成员
* index：取下标

而单目运算符有以下种类：

* pos：正
* neg：负
* cpl：按位取反
* not：逻辑非
* arr：组成为数列

##### 示例

假设有如下代码：

```
class c {
    def a = func {
        return 114;
    };
    func f(x) {
        return { 2*x+1, 3*x+2 };
    }
}


obj = c.new;

rst = obj.f(obj.a())[0];
```

那么其对应的STVC应为：

```
function #1:
return 114
end

function #2: x
.1 = 2 mul x
.2 = .1 add 1
.3 = 3 mul x
.4 = .3 add 2
list .5 .2 .4
return .5
end

class c: a f
a = #1
f = #2
end

new obj c

.1 = obj member a
call .2 .1
.3 = obj member f
call .4 .3: .2
.5 = .4 index 0
rst = .5
```