	.text
.global func1
func1:
	nop
	push %rbp
	mov %rsp, %rbp
	push %rdi
	push %rsi
	sub $8, %rsp
	.file 1 "main.c"
	.loc 1 3 0
	# }
	.loc 1 2 0
	#   return c;
	movslq -8(%rbp), %rax
	push %rax
	movslq -16(%rbp), %rax
	mov %rax, %rcx
	pop %rax
	add %rcx, %rax
	mov %eax, -24(%rbp)
	.loc 1 3 0
	# }
	.loc 1 2 0
	#   return c;
	movslq -24(%rbp), %rax
	cltq
	leave
	ret
	leave
	ret
	.text
.global func2
func2:
	nop
	push %rbp
	mov %rsp, %rbp
	sub $24, %rsp
	.loc 1 10 0
	# }
	.loc 1 7 0
	#   int b = 20;
	movl $10, -8(%rbp)
	.loc 1 8 0
	#   int c = func1(a, b);
	movl $20, -16(%rbp)
	.loc 1 9 0
	#   return c;
	push %rdi
	push %rsi
	sub $8, %rsp
	.loc 1 7 0
	#   int b = 20;
	movslq -8(%rbp), %rax
	push %rax
	.loc 1 8 0
	#   int c = func1(a, b);
	movslq -16(%rbp), %rax
	push %rax
	pop %rsi
	pop %rdi
	call func1
	add $8, %rsp
	pop %rsi
	pop %rdi
	mov %eax, -24(%rbp)
	.loc 1 10 0
	# }
	.loc 1 9 0
	#   return c;
	movslq -24(%rbp), %rax
	cltq
	leave
	ret
	leave
	ret
	.text
.global main
main:
	nop
	push %rbp
	mov %rsp, %rbp
	sub $8, %rsp
	.loc 1 15 0
	# }
	.loc 1 14 0
	#   return 0;
	sub $8, %rsp
	call func2
	add $8, %rsp
	mov %eax, -8(%rbp)
	.loc 1 15 0
	# }
	mov $0, %rax
	cltq
	leave
	ret
	leave
	ret
