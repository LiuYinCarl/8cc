# 8cc 笔记

## 工具准备

* 编码编辑器: Emacs
* 编译器: GCC
* 调试器: GDB + [gdb-dashboard](https://github.com/LiuYinCarl/gdb-dashboard)


## 关注的问题

1. 8cc 是不是单趟扫描，如果是的话，简要流程是什么
2. 8cc 是否构建了符号表，符号表的作用是什么
3. 8cc 的符号查找是怎么实现的
4. 8cc 的 AST 和符号表有什么关系
5. 8cc 如何依赖 AST 来生成汇编代码
6. 8cc 生成汇编代码时如何处理条件/分支/循环语句


## 时序图

使用 `|+` 表示多个过程具有时序关系。

```txt
.
|+ func1()
|
|+ func2()
```

使用 `|-` 表示多个过程具有 OR 的关系，每次只执行其中一个。
```txt
.
|- func1()
|
|- func2()
```

使用 `|?` 表示可能存在的过程，需要根据条件决定是否执行。
```txt
.
|? func1()
```

### 源代码到汇编时序图

```txt
.
|
|+ parseopt(argc, argv) 解析程序输入参数
|
|+ lex_init(infile) 词法分析器初始化
|  |
|  |+ stream_push(make_file(fp, filename)) 输入源文件存入 files 数组
|
|+ cpp_init()
|  |
|  |+ init_keywords() 初始化 C 语言关键字
|  |
|  |+ init_now() 初始化当前时间信息
|  |
|  |+ init_predefined_macros()
|     |
|     |+ 常见系统 include 目录存入 std_include_path
|     |
|     |+ define_special_macro() 定义特殊宏处理函数
|     |
|     |+ read_from_string("#include <" BUILD_DIR "/include/8cc.h>") 加载 8cc.h
|
|+ parse_init()
|  |
|  |+ define_builtin() 定义了 4 个特殊的内建函数，作为 ast_gvars 存入 globalenv
|
|+ set_output_file(open_asmfile())
|
|+ read_toplevels()
|  |
|  |+ vec_push(toplevels, read_funcdef()) 读入函数定义
|  |
|  |+ read_decl(toplevels, true) 读入非函数定义，struct 之类
|
|+ emit_toplevel()
|
|+ close_output_file()
|
-
```

## 构建 AST 流程图

构建 AST 的过程是从 `read_toplevels` 函数开始进行。

```txt
read_toplevels
|
|- read_funcdef() 读到函数定义会放入 Vector toplevels
|  |
|  |+ read_decl_spec_opt()
|  |  |
|  |  |? read_decl_spec()
|  |
|  |+ make_map_parent(globalenv) 创建局部作用域，父作用域为 globalenv
|  |
|  |+ read_declarator() 读取函数定义(TODO: 这里有一个新旧式函数定义的区别)
|  |
|  |+ ast_gvar(functype, name) 函数名存入 globalenv
|  |
|  |? read_func_body(functype, name, params) 读取函数体定义
|  |
|  |+ backfill_labels() 填充 labels(用于 goto)
|
|- read_decl() 读到其他结构定义也放入 Vector toplevels


```

## AST 生成汇编代码流程图

```txt

```


## 8cc 的启动流程

以如下源代码为例

```c
// mytest/main.c

int main() {
	return 0;
}
```

使用如下命令编译

```bash
./8cc -S -o main.asm main.c
```

那么启动时候的堆栈如下

```gdb
[0] from 0x0000555555564c28 in lex+8 at lex.c:597
[1] from 0x0000555555557b78 in read_expand_newline+13 at cpp.c:328
[2] from 0x0000555555557d68 in read_expand+18 at cpp.c:366
[3] from 0x000055555555a6bb in read_token+13 at cpp.c:1006
[4] from 0x000055555555a698 in peek_token+13 at cpp.c:998
[5] from 0x000055555556e7e2 in peek+9 at parse.c:2750
[6] from 0x000055555556e5d0 in read_toplevels+21 at parse.c:2707
[7] from 0x000055555555a648 in read_from_string+37 at cpp.c:991
[8] from 0x000055555555a52b in init_predefined_macros+394 at cpp.c:956
[9] from 0x000055555555a5b1 in cpp_init+49 at cpp.c:968
[10] from 0x0000555555556b20 in main+121 at main.c:173
```

需要注意的栈是

```gdb
[8] from 0x000055555555a52b in init_predefined_macros+394 at cpp.c:956
```

这个函数的源码如下

```c
// cpp.c

static void init_predefined_macros() {
    vec_push(std_include_path, BUILD_DIR "/include");
    vec_push(std_include_path, "/usr/local/lib/8cc/include");
    vec_push(std_include_path, "/usr/local/include");
    vec_push(std_include_path, "/usr/include");
    vec_push(std_include_path, "/usr/include/linux");
    vec_push(std_include_path, "/usr/include/x86_64-linux-gnu");

    define_special_macro("__DATE__", handle_date_macro);
    define_special_macro("__TIME__", handle_time_macro);
    define_special_macro("__FILE__", handle_file_macro);
    define_special_macro("__LINE__", handle_line_macro);
    define_special_macro("_Pragma",  handle_pragma_macro);
    // [GNU] Non-standard macros
    define_special_macro("__BASE_FILE__", handle_base_file_macro);
    define_special_macro("__COUNTER__", handle_counter_macro);
    define_special_macro("__INCLUDE_LEVEL__", handle_include_level_macro);
    define_special_macro("__TIMESTAMP__", handle_timestamp_macro);

    read_from_string("#include <" BUILD_DIR "/include/8cc.h>");
}
```

这个函数做了三件事

1. 将系统头文件路径放入 `std_include_path` 中，以供后续查找头文件
2. 为一些特殊的标准宏和 GUN 拓展宏定义了处理函数
3. 导入了 `/include/8cc.h` 这个头文件

我们暂时只关注导入 `/include/8cc.h` 这个头文件。8cc 在启动任何编译时都把 `8cc.h` 这个头文件
作为了隐式导入的第一个头文件。

这一步可以很简单的进行验证。

在 `include/8cc.h` 文件最后定义一个宏

```c
// include/8cc.h

#define hello_8cc_h 10086
```

修改上面的 main.c 测试文件

```c
// mytest/mian.c

int main() {
  int x = hello_8cc_h;
  return 0;
}
```

编译后查看生产的 `main.asm` 文件

```asm
; mytest/main.asm

	.text
.global main
main:
	nop
	push %rbp
	mov %rsp, %rbp
	sub $8, %rsp
	.file 1 "main.c"
	.loc 1 3 0
	# }
	.loc 1 2 0
	#   return 0;
	movl $10086, -8(%rbp) ; 这里可以看到我们定义的 hello_8cc_h 宏的值
	.loc 1 3 0
	# }
	mov $0, %rax
	cltq
	leave
	ret
	leave
	ret
```

查看 `include/8cc.h` 头文件，可以发现这个文件主要就是定义了一些常量。

## 汇编

8cc 直接使用了 as 作为汇编器，可以在 `main` 函数中找到相关代码。
