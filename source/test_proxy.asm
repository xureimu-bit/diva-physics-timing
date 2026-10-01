EXTERN AnimationDeltaProxy:PROC
PUBLIC TestProxyRegisters
.const
values DWORD 3F800000h,40000000h,40400000h,40800000h
.code
TestProxyRegisters PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp,20h
    .allocstack 20h
    .endprolog
    mov rbx,rcx
    mov rax,11111111h
    mov rcx,22222222h
    mov rdx,33333333h
    mov r8,44444444h
    mov r9,55555555h
    mov r10,66666666h
    mov r11,77777777h
    movups xmm1,values
    movups xmm2,values
    movups xmm3,values
    movups xmm4,values
    movups xmm5,values
    cmp rax,rax
    pushfq
    pop qword ptr [rbx+56]
    call AnimationDeltaProxy
    mov [rbx],rax
    mov [rbx+8],rcx
    mov [rbx+16],rdx
    mov [rbx+24],r8
    mov [rbx+32],r9
    mov [rbx+40],r10
    mov [rbx+48],r11
    pushfq
    pop qword ptr [rbx+64]
    movups [rbx+80],xmm0
    movups [rbx+96],xmm1
    movups [rbx+112],xmm2
    movups [rbx+128],xmm3
    movups [rbx+144],xmm4
    movups [rbx+160],xmm5
    add rsp,20h
    pop rbx
    ret
TestProxyRegisters ENDP
END
