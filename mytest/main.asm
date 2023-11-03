	.text
.global foo
foo:
	nop
	push %rbp
	mov %rsp, %rbp
	push %rdi
	push %rsi
	leave
	ret
	.text
.global main
main:
	nop
	push %rbp
	mov %rsp, %rbp
	.file 1 "main.c"
	.loc 1 1 0
	# #define M1(arg1, arg2) FUNCNAME(arg1, arg2)
	push %rdi
	push %rsi
	mov $21, %rax
	push %rax
	mov $31, %rax
	push %rax
	pop %rsi
	pop %rdi
	call foo
	pop %rsi
	pop %rdi
	leave
	ret
