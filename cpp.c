// Copyright 2012 Rui Ueyama. Released under the MIT license.

/*
 * This implements Dave Prosser's C Preprocessing algorithm, described
 * in this document: https://github.com/rui314/8cc/wiki/cpp.algo.pdf
 */

#include <ctype.h>
#include <libgen.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "8cc.h"

static Map *macros = &EMPTY_MAP;
static Map *once = &EMPTY_MAP; // 使用了 #pragma once, 只允许 include 一次的文件
static Map *keywords = &EMPTY_MAP;
static Map *include_guard = &EMPTY_MAP; // TODO ?
static Vector *cond_incl_stack = &EMPTY_VECTOR; // TODO ?
static Vector *std_include_path = &EMPTY_VECTOR;
static struct tm now;
static Token *cpp_token_zero = &(Token){ .kind = TNUMBER, .sval = "0" };
static Token *cpp_token_one = &(Token){ .kind = TNUMBER, .sval = "1" };

typedef void SpecialMacroHandler(Token *tok);
typedef enum { IN_THEN, IN_ELIF, IN_ELSE } CondInclCtx;
typedef enum { MACRO_OBJ, MACRO_FUNC, MACRO_SPECIAL } MacroType;

typedef struct {
    CondInclCtx ctx;
    char *include_guard;
    File *file;
    bool wastrue; // TODO 这个表示什么
} CondIncl;

typedef struct {
    MacroType kind;
    int nargs;
    Vector *body; // type of element is Token*
    bool is_varg;
    SpecialMacroHandler *fn;
} Macro;

static Macro *make_obj_macro(Vector *body);
static Macro *make_func_macro(Vector *body, int nargs, bool is_varg);
static Macro *make_special_macro(SpecialMacroHandler *fn);
static void define_obj_macro(char *name, Token *value);
static void read_directive(Token *hash);
static Token *read_expand(void);

/*
 * Constructors
 */

static CondIncl *make_cond_incl(bool wastrue) {
    CondIncl *r = calloc(1, sizeof(CondIncl));
    r->ctx = IN_THEN;
    r->wastrue = wastrue;
    return r;
}

static Macro *make_macro(Macro *tmpl) {
    Macro *r = malloc(sizeof(Macro));
    *r = *tmpl;
    return r;
}

static Macro *make_obj_macro(Vector *body) {
    return make_macro(&(Macro){ MACRO_OBJ, .body = body });
}

static Macro *make_func_macro(Vector *body, int nargs, bool is_varg) {
    return make_macro(&(Macro){
            MACRO_FUNC, .nargs = nargs, .body = body, .is_varg = is_varg });
}

static Macro *make_special_macro(SpecialMacroHandler *fn) {
    return make_macro(&(Macro){ MACRO_SPECIAL, .fn = fn });
}

// 按照位置保存宏参数
static Token *make_macro_token(int position, bool is_vararg) {
    Token *r = malloc(sizeof(Token));
    r->kind = TMACRO_PARAM;
    r->is_vararg = is_vararg;
    r->hideset = NULL;
    r->position = position;
    r->space = false;
    r->bol = false;
    return r;
}

static Token *copy_token(Token *tok) {
    Token *r = malloc(sizeof(Token));
    *r = *tok;
    return r;
}

// 判断下一个关键字是否是 id, 不回退
static void expect(char id) {
    Token *tok = lex();
    if (!is_keyword(tok, id))
        errort(tok, "%c expected, but got %s", id, tok2s(tok));
}

/*
 * Utility functions
 */
// 判断一个 Token 是不是标识符并且等于 s
bool is_ident(Token *tok, char *s) {
    return tok->kind == TIDENT && !strcmp(tok->sval, s);
}

// 判断下一个 Token 是不是等于关键字 id
// 如果是，则返回 True 并消耗下一个 Token
// 否则返回 False 并回退 Token
static bool next(int id) {
    Token *tok = lex();
    if (is_keyword(tok, id))
        return true;
    unget_token(tok);
    return false;
}

// 将 tokens Vector 中的第一个 Token 的 space 属性设置为 tmpl->space
static void propagate_space(Vector *tokens, Token *tmpl) {
    if (vec_len(tokens) == 0)
        return;
    Token *tok = copy_token(vec_head(tokens));
    tok->space = tmpl->space;
    vec_set(tokens, 0, tok);
}

/*
 * Macro expander
 */

// 读取一个类型为标识符的 Token
static Token *read_ident() {
    Token *tok = lex();
    if (tok->kind != TIDENT)
        errort(tok, "identifier expected, but got %s", tok2s(tok));
    return tok;
}

// 跳过一个换行符
void expect_newline() {
    Token *tok = lex();
    if (tok->kind != TNEWLINE)
        errort(tok, "newline expected, but got %s", tok2s(tok));
}
// 读取一个参数
// 返回值：
static Vector *read_one_arg(Token *ident, bool *end, bool readall) {
    // TODO 这个是什么
    Vector *r = make_vector();
    // 括号的层级
    int level = 0;
    for (;;) {
        // 读取一个 Token
        Token *tok = lex();
        // 如果是 EOF，报错
        if (tok->kind == TEOF)
            errort(ident, "unterminated macro argument list");
        // 如果是换行符，进入下一次循环
        if (tok->kind == TNEWLINE)
            continue;
        // 如果是行的开头并且 Token 是 '#', 那么读取宏定义并且进入下一次循环
        if (tok->bol && is_keyword(tok, '#')) {
            read_directive(tok);
            continue;
        }
        // 如果不在嵌套的小括号内并且 Token 是 ')',
        // 那么回退这个 Token, 并且设置 end 值为 true(这是最外层的')',很明显到了定义的尾部)
        // 返回 r
        if (level == 0 && is_keyword(tok, ')')) {
            unget_token(tok);
            *end = true;
            return r;
        }
        // 如果不在嵌套的小括号内并且 Token 是 ',' 并且 readall 是 false
        // 返回 r
        // 这里读到了 ',' 并且不在内层的小括号中, 说明在这里第一个参数被读取完
        if (level == 0 && is_keyword(tok, ',') && !readall)
            return r;
        // '(' ')' 都不是参数，所以不会被作为 Token 读入 r
        if (is_keyword(tok, '('))
            level++;
        if (is_keyword(tok, ')'))
            level--;
        // C11 6.10.3p10: Within the macro argument list,
        // newline is considered a normal whitespace character.
        // I don't know why the standard specifies such a minor detail,
        // but the difference of newline and space is observable
        // if you stringize tokens using #.
        if (tok->bol) {
            tok = copy_token(tok); // 这句应该是多余的
            tok->bol = false;
            tok->space = true;
        }
        // 把当前 Token 放到 r
        vec_push(r, tok);
    }
}

// 读取多个宏参数
static Vector *do_read_args(Token *ident, Macro *macro) {
    // 参数列表
    Vector *r = make_vector();
    // 是否读取完所有参数
    bool end = false;
    while (!end) {
        // TODO 为什么需要判断是否在省略号内
        bool in_ellipsis = (macro->is_varg && vec_len(r) + 1 == macro->nargs);
        vec_push(r, read_one_arg(ident, &end, in_ellipsis));
    }
    // TODO 为什么在可变参数的情况下需要补齐参数数量，是不是因为可变参数默认不当作一个参数，所以需要手动填充？
    if (macro->is_varg && vec_len(r) == macro->nargs - 1)
        vec_push(r, make_vector());
    return r;
}

// 读取宏参数
// 如果一个宏 M 没有参数，那么它的参数列表就是一个空 Vector
// 如果一个宏 M 有一个参数，那么它的参数列表是一个包含一个空 Vector 的 Vector
static Vector *read_args(Token *tok, Macro *macro) {
    if (macro->nargs == 0 && is_keyword(peek_token(), ')')) {
        // If a macro M has no parameter, argument list of M()
        // is an empty list. If it has one parameter,
        // argument list of M() is a list containing an empty list.
        return make_vector();
    }
    Vector *args = do_read_args(tok, macro);
    if (vec_len(args) != macro->nargs)
        errort(tok, "macro argument number does not match");
    return args;
}

// 添加隐藏集
// 将 tokens 中的每个 Token 的隐藏集和 hideset 做并集
static Vector *add_hide_set(Vector *tokens, Set *hideset) {
    Vector *r = make_vector();
    for (int i = 0; i < vec_len(tokens); i++) {
        Token *t = copy_token(vec_get(tokens, i));
        t->hideset = set_union(t->hideset, hideset);
        vec_push(r, t);
    }
    return r;
}

// 连接 Token t 和 u 并返回一个新的 Token
static Token *glue_tokens(Token *t, Token *u) {
    Buffer *b = make_buffer();
    buf_printf(b, "%s", tok2s(t));
    buf_printf(b, "%s", tok2s(u));
    Token *r = lex_string(buf_body(b));
    return r;
}

// 将 tokens Vector 的最后一个 Token 和 tok 连接起来
static void glue_push(Vector *tokens, Token *tok) {
    Token *last = vec_pop(tokens);
    vec_push(tokens, glue_tokens(last, tok));
}

// 将 args Vector 中的 Token 合并成一个字符串保存在 tmpl 中并返回
static Token *stringize(Token *tmpl, Vector *args) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(args); i++) {
        Token *tok = vec_get(args, i);
        // 如果 buffer 不为空并且 Token 有前缀空格，就往 buffer 里写一个空格
        if (buf_len(b) && tok->space)
            buf_printf(b, " ");
        buf_printf(b, "%s", tok2s(tok));
    }
    buf_write(b, '\0');
    Token *r = copy_token(tmpl);
    r->kind = TSTRING;
    r->sval = buf_body(b);
    r->slen = buf_len(b);
    r->enc = ENC_NONE;
    return r;
}

// TODO
static Vector *expand_all(Vector *tokens, Token *tmpl) {
    token_buffer_stash(vec_reverse(tokens));
    Vector *r = make_vector();
    for (;;) {
        Token *tok = read_expand();
        if (tok->kind == TEOF)
            break;
        vec_push(r, tok);
    }
    propagate_space(r, tmpl);
    token_buffer_unstash();
    return r;
}

static Vector *subst(Macro *macro, Vector *args, Set *hideset) {
    Vector *r = make_vector();
    int len = vec_len(macro->body);
    for (int i = 0; i < len; i++) {
        Token *t0 = vec_get(macro->body, i);
        Token *t1 = (i == len - 1) ? NULL : vec_get(macro->body, i + 1);
        bool t0_param = (t0->kind == TMACRO_PARAM);
        bool t1_param = (t1 && t1->kind == TMACRO_PARAM);

        if (is_keyword(t0, '#') && t1_param) {
            vec_push(r, stringize(t0, vec_get(args, t1->position)));
            i++;
            continue;
        }
        if (is_keyword(t0, KHASHHASH) && t1_param) {
            Vector *arg = vec_get(args, t1->position);
            // [GNU] [,##__VA_ARG__] is expanded to the empty token sequence
            // if __VA_ARG__ is empty. Otherwise it's expanded to
            // [,<tokens in __VA_ARG__>].
            if (t1->is_vararg && vec_len(r) > 0 && is_keyword(vec_tail(r), ',')) {
                if (vec_len(arg) > 0)
                    vec_append(r, arg);
                else
                    vec_pop(r);
            } else if (vec_len(arg) > 0) {
                glue_push(r, vec_head(arg));
                for (int i = 1; i < vec_len(arg); i++)
                    vec_push(r, vec_get(arg, i));
            }
            i++;
            continue;
        }
        if (is_keyword(t0, KHASHHASH) && t1) {
            hideset = t1->hideset;
            glue_push(r, t1);
            i++;
            continue;
        }
        if (t0_param && t1 && is_keyword(t1, KHASHHASH)) {
            hideset = t1->hideset;
            Vector *arg = vec_get(args, t0->position);
            if (vec_len(arg) == 0)
                i++;
            else
                vec_append(r, arg);
            continue;
        }
        if (t0_param) {
            Vector *arg = vec_get(args, t0->position);
            vec_append(r, expand_all(arg, t0));
            continue;
        }
        vec_push(r, t0);
    }
    return add_hide_set(r, hideset);
}

static void unget_all(Vector *tokens) {
    for (int i = vec_len(tokens) - 1; i >= 0; i--)
        unget_token(vec_get(tokens, i));
}

// TODO 8cc 里预处理器用的地方比较奇怪
// 它的流程如下
// 如果扫描 Token 的过程中遇到了宏定义，就保存在 macros Map
// 之后扫描其他 Token 的时候，每次检查这个 Token 是不是一个宏
// 如果是的话，就进行替换
// This is "expand" function in the Dave Prosser's document.
static Token *read_expand_newline() {
    Token *tok = lex();
    // 获取一个 Token, 如果不是标识符类型，直接返回
    if (tok->kind != TIDENT)
        return tok;
    // 如果是标识符类型, 检查下这个名字是不是一个宏定义
    char *name = tok->sval;
    Macro *macro = map_get(macros, name);
    // 如果不是一个宏或者是一个宏, 但是这个名字在它自己的隐藏集里(不用进行替换)
    // 那也可以直接返回这个 Token
    if (!macro || set_has(tok->hideset, name))
        return tok;

    switch (macro->kind) {
    case MACRO_OBJ: {
        // 把 Token 名字加到它自己的隐藏集里
        Set *hideset = set_add(tok->hideset, name);
        Vector *tokens = subst(macro, NULL, hideset);
        propagate_space(tokens, tok);
        // 把所有 Token 回退
        unget_all(tokens);
        // 重新读取一个 Token
        return read_expand();
    }
    case MACRO_FUNC: {
        // TODO 为什么可以直接返回
        if (!next('('))
            return tok;
        // 读取这个宏调用的所有参数
        Vector *args = read_args(tok, macro);
        Token *rparen = peek_token();
        expect(')');
        // 计算当前 Token 和下一个 Token 的隐藏集的交集，并把当前 Token 的名字加入到交集中
        Set *hideset = set_add(set_intersection(tok->hideset, rparen->hideset), name);
        Vector *tokens = subst(macro, args, hideset);
        propagate_space(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_SPECIAL:
        // 查找 define_special_macro 看各种特殊宏的定义
        // 其实就是把宏(如 __FILE__)替换为了对应的信息, 然后塞回到 Token 缓冲
        // 调用特殊宏对应的处理函数
        macro->fn(tok);
        // 重新读取一个 Token
        return read_expand();
    default:
        error("internal error");
    }
}

// 读取一个新的 Token, 不包括换行
static Token *read_expand() {
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind != TNEWLINE)
            return tok;
    }
}

// 处理宏参数
// 例子见 example/cpp_1.c
static bool read_funclike_macro_params(Token *name, Map *param) {
    // 当前在处理第几个参数
    int pos = 0;
    for (;;) {
        // 读取下一个 Token
        Token *tok = lex();
        // 读到右括号
        if (is_keyword(tok, ')'))
            return false;
        if (pos) {
            // 如果读到的不是 ',' 说明语法错误
            if (!is_keyword(tok, ','))
                errort(tok, ", expected, but got %s", tok2s(tok));
            // 丢掉 ',' 读取下一个 Token
            tok = lex();
        }
        // 如果读到换行 Token, 说明语法错误
        if (tok->kind == TNEWLINE)
            errort(name, "missing ')' in macro parameter list");
        // 如果读到省略号 Token, 那么在 param Map 中增加一个可变参数键值对
        if (is_keyword(tok, KELLIPSIS)) {
            map_put(param, "__VA_ARGS__", make_macro_token(pos++, true));
            // 可变参数的下一个 Token 应该是右括号
            expect(')');
            return true;
        }
        // 如果读到的不是标识符，说明语法错误
        if (tok->kind != TIDENT)
            errort(tok, "identifier expected, but got %s", tok2s(tok));
        char *arg = tok->sval;
        // 如果接下来是省略号 Token 和右括号, 那么在 param Map 中增加一个 key 为
        // arg 的键值对
        // TODO 例子是什么
        if (next(KELLIPSIS)) {
            expect(')');
            map_put(param, arg, make_macro_token(pos++, true));
            return true;
        }
        map_put(param, arg, make_macro_token(pos++, false));
    }
}

// 检查 '##' 的位置是不是合法，不能出现宏展开的开头或者结尾
static void hashhash_check(Vector *v) {
    if (vec_len(v) == 0)
        return;
    if (is_keyword(vec_head(v), KHASHHASH))
        errort(vec_head(v), "'##' cannot appear at start of macro expansion");
    if (is_keyword(vec_tail(v), KHASHHASH))
        errort(vec_tail(v), "'##' cannot appear at end of macro expansion");
}

// 读带参数的宏定义的替换部分
static Vector *read_funclike_macro_body(Map *param) {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = lex();
        // 读到了换行符，说明宏定义结束了
        if (tok->kind == TNEWLINE)
            return r;
        if (tok->kind == TIDENT) {
            // TODO 这里是在进行宏拓展吗
            Token *subst = map_get(param, tok->sval);
            if (subst) {
                subst = copy_token(subst);
                subst->space = tok->space;
                vec_push(r, subst);
                continue;
            }
        }
        vec_push(r, tok);
    }
}

// 解析 MACRO FUNCTION
static void read_funclike_macro(Token *name) {
    Map *param = make_map();
    bool is_varg = read_funclike_macro_params(name, param);
    Vector *body = read_funclike_macro_body(param);
    hashhash_check(body);
    Macro *macro = make_func_macro(body, map_len(param), is_varg);
    map_put(macros, name->sval, macro);
}

// 解析 MACRO OBJECT
static void read_obj_macro(char *name) {
    Vector *body = make_vector();
    for (;;) {
        Token *tok = lex();
        if (tok->kind == TNEWLINE)
            break;
        vec_push(body, tok);
    }
    hashhash_check(body);
    map_put(macros, name, make_obj_macro(body));
}

/*
 * #define
 */

// 读取宏定义，根据宏名字后是不是有 '(' 判断是 MACRO FUNCTION 还是 MACRO OBJECT
static void read_define() {
    Token *name = read_ident();
    Token *tok = lex();
    if (is_keyword(tok, '(') && !tok->space) {
        read_funclike_macro(name);
        return;
    }
    unget_token(tok);
    read_obj_macro(name->sval);
}

/*
 * #undef
 */

static void read_undef() {
    Token *name = read_ident();
    expect_newline();
    map_remove(macros, name->sval);
}

/*
 * #if and the like
 */
// 读取 #if/ifdef MACRO_NAME 格式
// 返回这个宏是否被定义
static Token *read_defined_op() {
    Token *tok = lex();
    if (is_keyword(tok, '(')) {
        tok = lex();
        expect(')');
    }
    if (tok->kind != TIDENT)
        errort(tok, "identifier expected, but got %s", tok2s(tok));
    // 从宏 Map 中查找下这个宏是不是已经被定义
    return map_get(macros, tok->sval) ? cpp_token_one : cpp_token_zero;
}

// TODO 这里是在做替换？
static Vector *read_intexpr_line() {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind == TNEWLINE)
            return r;
        if (is_ident(tok, "defined")) {
            vec_push(r, read_defined_op());
        } else if (tok->kind == TIDENT) {
            // C11 6.10.1.4 says that remaining identifiers
            // should be replaced with pp-number 0.
            vec_push(r, cpp_token_zero);
        } else {
            vec_push(r, tok);
        }
    }
}

static bool read_constexpr() {
    token_buffer_stash(vec_reverse(read_intexpr_line()));
    Node *expr = read_expr();
    Token *tok = lex();
    if (tok->kind != TEOF)
        errort(tok, "stray token: %s", tok2s(tok));
    token_buffer_unstash();
    return eval_intexpr(expr, NULL);
}

static void do_read_if(bool istrue) {
    vec_push(cond_incl_stack, make_cond_incl(istrue));
    if (!istrue)
        skip_cond_incl();
}

static void read_if() {
    do_read_if(read_constexpr());
}

static void read_ifdef() {
    Token *tok = lex();
    if (tok->kind != TIDENT)
        errort(tok, "identifier expected, but got %s", tok2s(tok));
    expect_newline();
    do_read_if(map_get(macros, tok->sval));
}

static void read_ifndef() {
    Token *tok = lex();
    if (tok->kind != TIDENT)
        errort(tok, "identifier expected, but got %s", tok2s(tok));
    expect_newline();
    do_read_if(!map_get(macros, tok->sval));
    if (tok->count == 2) {
        // "ifndef" is the second token in this file.
        // Prepare to detect an include guard.
        CondIncl *ci = vec_tail(cond_incl_stack);
        ci->include_guard = tok->sval;
        ci->file = tok->file;
    }
}

static void read_else(Token *hash) {
    if (vec_len(cond_incl_stack) == 0)
        errort(hash, "stray #else");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        errort(hash, "#else appears in #else");
    expect_newline();
    ci->ctx = IN_ELSE;
    ci->include_guard = NULL;
    if (ci->wastrue)
        skip_cond_incl();
}

static void read_elif(Token *hash) {
    if (vec_len(cond_incl_stack) == 0)
        errort(hash, "stray #elif");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        errort(hash, "#elif after #else");
    ci->ctx = IN_ELIF;
    ci->include_guard = NULL;
    if (ci->wastrue || !read_constexpr()) {
        skip_cond_incl();
        return;
    }
    ci->wastrue = true;
}

// Skips all newlines and returns the first non-newline token.
static Token *skip_newlines() {
    Token *tok = lex();
    while (tok->kind == TNEWLINE)
        tok = lex();
    unget_token(tok);
    return tok;
}

static void read_endif(Token *hash) {
    if (vec_len(cond_incl_stack) == 0)
        errort(hash, "stray #endif");
    CondIncl *ci = vec_pop(cond_incl_stack);
    expect_newline();

    // Detect an #ifndef and #endif pair that guards the entire
    // header file. Remember the macro name guarding the file
    // so that we can skip the file next time.
    if (!ci->include_guard || ci->file != hash->file)
        return;
    Token *last = skip_newlines();
    if (ci->file != last->file)
        map_put(include_guard, ci->file->name, ci->include_guard);
}

/*
 * #error and #warning
 */

static char *read_error_message() {
    Buffer *b = make_buffer();
    for (;;) {
        Token *tok = lex();
        if (tok->kind == TNEWLINE)
            return buf_body(b);
        if (buf_len(b) != 0 && tok->space)
            buf_write(b, ' ');
        buf_printf(b, "%s", tok2s(tok));
    }
}

static void read_error(Token *hash) {
    errort(hash, "#error: %s", read_error_message());
}

static void read_warning(Token *hash) {
    warnt(hash, "#warning: %s", read_error_message());
}

/*
 * #include
 */
// 将 args 中的参数合并成一个字符串
static char *join_paths(Vector *args) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(args); i++)
        buf_printf(b, "%s", tok2s(vec_get(args, i)));
    return buf_body(b);
}

// 解析 #include 宏中的头文件名字
static char *read_cpp_header_name(Token *hash, bool *std) {
    // Try reading a filename using a special tokenizer for #include.
    char *path = read_header_file_name(std);
    if (path)
        return path;

    // If a token following #include does not start with < nor ",
    // try to read the token as a regular token. Macro-expanded
    // form may be a valid header file path.
    Token *tok = read_expand_newline();
    if (tok->kind == TNEWLINE)
        errort(hash, "expected filename, but got newline");
    if (tok->kind == TSTRING) {
        *std = false;
        return tok->sval;
    }
    if (!is_keyword(tok, '<'))
        errort(tok, "< expected, but got %s", tok2s(tok));
    Vector *tokens = make_vector();
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind == TNEWLINE)
            errort(hash, "premature end of header name");
        if (is_keyword(tok, '>'))
            break;
        vec_push(tokens, tok);
    }
    *std = true;
    return join_paths(tokens);
}

// 检查一个文件是不是已经被包含过了
// TODO 这是不是 8cc 自己的头文件
static bool guarded(char *path) {
    char *guard = map_get(include_guard, path);
    bool r = (guard && map_get(macros, guard));
    define_obj_macro("__8cc_include_guard", r ? cpp_token_one : cpp_token_zero);
    return r;
}

// 尝试 include 一个文件
// TODO isimport 是啥
static bool try_include(char *dir, char *filename, bool isimport) {
    char *path = fullpath(format("%s/%s", dir, filename));
    if (map_get(once, path))
        return true;
    if (guarded(path))
        return true;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    if (isimport)
        map_put(once, path, (void *)1);
    stream_push(make_file(fp, path));
    return true;
}

static void read_include(Token *hash, File *file, bool isimport) {
    bool std;
    char *filename = read_cpp_header_name(hash, &std);
    expect_newline(); // TODO 为什么这里会需要读换行符
    if (filename[0] == '/') {
        // 如果文件名以 '/' 开头，尝试从根目录下找
        if (try_include("/", filename, isimport))
            return;
        goto err;
    }
    if (!std) {
        // 尝试从当前目录下找到这个头文件
        char *dir = file->name ? dirname(strdup(file->name)) : ".";
        if (try_include(dir, filename, isimport))
            return;
    }
    // 尝试从各个标准目录下找到这个头文件
    for (int i = 0; i < vec_len(std_include_path); i++)
        if (try_include(vec_get(std_include_path, i), filename, isimport))
            return;
  err:
    errort(hash, "cannot find header file: %s", filename);
}

static void read_include_next(Token *hash, File *file) {
    // [GNU] #include_next is a directive to include the "next" file
    // from the search path. This feature is used to override a
    // header file without getting into infinite inclusion loop.
    // This directive doesn't distinguish <> and "".
    bool std;
    char *filename = read_cpp_header_name(hash, &std);
    expect_newline();
    if (filename[0] == '/') {
        if (try_include("/", filename, false))
            return;
        goto err;
    }
    char *cur = fullpath(file->name);
    int i = 0;
    for (; i < vec_len(std_include_path); i++) {
        char *dir = vec_get(std_include_path, i);
        if (!strcmp(cur, fullpath(format("%s/%s", dir, filename))))
            break;
    }
    for (i++; i < vec_len(std_include_path); i++)
        if (try_include(vec_get(std_include_path, i), filename, false))
            return;
  err:
    errort(hash, "cannot find header file: %s", filename);
}

/*
 * #pragma
 */

static void parse_pragma_operand(Token *tok) {
    char *s = tok->sval;
    if (!strcmp(s, "once")) {
        char *path = fullpath(tok->file->name);
        map_put(once, path, (void *)1);
    } else if (!strcmp(s, "enable_warning")) {
        enable_warning = true;
    } else if (!strcmp(s, "disable_warning")) {
        enable_warning = false;
    } else {
        errort(tok, "unknown #pragma: %s", s);
    }
}

static void read_pragma() {
    Token *tok = read_ident();
    parse_pragma_operand(tok);
}

/*
 * #line
 */

static bool is_digit_sequence(char *p) {
    for (; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

static void read_line() {
    Token *tok = read_expand_newline();
    if (tok->kind != TNUMBER || !is_digit_sequence(tok->sval))
        errort(tok, "number expected after #line, but got %s", tok2s(tok));
    int line = atoi(tok->sval);
    tok = read_expand_newline();
    char *filename = NULL;
    if (tok->kind == TSTRING) {
        filename = tok->sval;
        expect_newline();
    } else if (tok->kind != TNEWLINE) {
        errort(tok, "newline or a source name are expected, but got %s", tok2s(tok));
    }
    File *f = current_file();
    f->line = line;
    if (filename)
        f->name = filename;
}

// GNU CPP outputs "# linenum filename flags" to preserve original
// source file information. This function reads them. Flags are ignored.
static void read_linemarker(Token *tok) {
    if (!is_digit_sequence(tok->sval))
        errort(tok, "line number expected, but got %s", tok2s(tok));
    int line = atoi(tok->sval);
    tok = lex();
    if (tok->kind != TSTRING)
        errort(tok, "filename expected, but got %s", tok2s(tok));
    char *filename = tok->sval;
    do {
        tok = lex();
    } while (tok->kind != TNEWLINE);
    File *file = current_file();
    file->line = line;
    file->name = filename;
}

/*
 * #-directive
 */

static void read_directive(Token *hash) {
    Token *tok = lex();
    if (tok->kind == TNEWLINE)
        return;
    if (tok->kind == TNUMBER) {
        read_linemarker(tok);
        return;
    }
    if (tok->kind != TIDENT)
        goto err;
    char *s = tok->sval;
    if (!strcmp(s, "define"))            read_define();
    else if (!strcmp(s, "elif"))         read_elif(hash);
    else if (!strcmp(s, "else"))         read_else(hash);
    else if (!strcmp(s, "endif"))        read_endif(hash);
    else if (!strcmp(s, "error"))        read_error(hash);
    else if (!strcmp(s, "if"))           read_if();
    else if (!strcmp(s, "ifdef"))        read_ifdef();
    else if (!strcmp(s, "ifndef"))       read_ifndef();
    else if (!strcmp(s, "import"))       read_include(hash, tok->file, true);
    else if (!strcmp(s, "include"))      read_include(hash, tok->file, false);
    else if (!strcmp(s, "include_next")) read_include_next(hash, tok->file);
    else if (!strcmp(s, "line"))         read_line();
    else if (!strcmp(s, "pragma"))       read_pragma();
    else if (!strcmp(s, "undef"))        read_undef();
    else if (!strcmp(s, "warning"))      read_warning(hash);
    else goto err;
    return;

  err:
    errort(hash, "unsupported preprocessor directive: %s", tok2s(tok));
}

/*
 * Special macros
 */

static void make_token_pushback(Token *tmpl, int kind, char *sval) {
    Token *tok = copy_token(tmpl);
    tok->kind = kind;
    tok->sval = sval;
    tok->slen = strlen(sval) + 1;
    tok->enc = ENC_NONE;
    unget_token(tok);
}

// 把日期信息一个字符串变为一个 Token, 写回到 Token 缓冲
static void handle_date_macro(Token *tmpl) {
    char buf[20];
    strftime(buf, sizeof(buf), "%b %e %Y", &now);
    make_token_pushback(tmpl, TSTRING, strdup(buf));
}

static void handle_time_macro(Token *tmpl) {
    char buf[10];
    strftime(buf, sizeof(buf), "%T", &now);
    make_token_pushback(tmpl, TSTRING, strdup(buf));
}

static void handle_timestamp_macro(Token *tmpl) {
    // [GNU] __TIMESTAMP__ is expanded to a string that describes the date
    // and time of the last modification time of the current source file.
    char buf[30];
    strftime(buf, sizeof(buf), "%a %b %e %T %Y", localtime(&tmpl->file->mtime));
    make_token_pushback(tmpl, TSTRING, strdup(buf));
}

static void handle_file_macro(Token *tmpl) {
    make_token_pushback(tmpl, TSTRING, tmpl->file->name);
}

static void handle_line_macro(Token *tmpl) {
    make_token_pushback(tmpl, TNUMBER, format("%d", tmpl->file->line));
}

static void handle_pragma_macro(Token *tmpl) {
    expect('(');
    Token *operand = read_token();
    if (operand->kind != TSTRING)
        errort(operand, "_Pragma takes a string literal, but got %s", tok2s(operand));
    expect(')');
    parse_pragma_operand(operand);
    make_token_pushback(tmpl, TNUMBER, "1");
}

static void handle_base_file_macro(Token *tmpl) {
    make_token_pushback(tmpl, TSTRING, get_base_file());
}

static void handle_counter_macro(Token *tmpl) {
    static int counter = 0;
    make_token_pushback(tmpl, TNUMBER, format("%d", counter++));
}

static void handle_include_level_macro(Token *tmpl) {
    make_token_pushback(tmpl, TNUMBER, format("%d", stream_depth() - 1));
}

/*
 * Initializer
 */

void add_include_path(char *path) {
    vec_push(std_include_path, path);
}

static void define_obj_macro(char *name, Token *value) {
    map_put(macros, name, make_obj_macro(make_vector1(value)));
}

static void define_special_macro(char *name, SpecialMacroHandler *fn) {
    map_put(macros, name, make_special_macro(fn));
}

static void init_keywords() {
#define op(id, str)         map_put(keywords, str, (void *)id);
#define keyword(id, str, _) map_put(keywords, str, (void *)id);
#include "keyword.inc"
#undef keyword
#undef op
}

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

void init_now() {
    time_t timet = time(NULL);
    localtime_r(&timet, &now);
}

void cpp_init() {
    setlocale(LC_ALL, "C");
    init_keywords();
    init_now();
    init_predefined_macros();
}

/*
 * Public intefaces
 */

static Token *maybe_convert_keyword(Token *tok) {
    if (tok->kind != TIDENT)
        return tok;
    int id = (intptr_t)map_get(keywords, tok->sval);
    if (!id)
        return tok;
    Token *r = copy_token(tok);
    r->kind = TKEYWORD;
    r->id = id;
    return r;
}

// Reads from a string as if the string is a content of input file.
// Convenient for evaluating small string snippet contaiing preprocessor macros.
void read_from_string(char *buf) {
    stream_stash(make_file_string(buf));
    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++)
        emit_toplevel(vec_get(toplevels, i));
    stream_unstash();
}

// 获取下一个 Token 的副本
Token *peek_token() {
    Token *r = read_token();
    unget_token(r);
    return r;
}

// 获取下一个 Token
Token *read_token() {
    Token *tok;
    for (;;) {
        tok = read_expand();
	// 如果下一个 Token 是 '#'，并且这个时候没有被宏屏蔽
	// 那么就读入 include 的头文件
        if (tok->bol && is_keyword(tok, '#') && tok->hideset == NULL) {
            read_directive(tok);
            continue;
        }
        assert(tok->kind < MIN_CPP_TOKEN);
        return maybe_convert_keyword(tok);
    }
}
