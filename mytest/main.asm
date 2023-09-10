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
	movl $10086, -8(%rbp)
	.loc 1 3 0
	# }
	mov $0, %rax
	cltq
	leave
	ret
	leave
	ret
