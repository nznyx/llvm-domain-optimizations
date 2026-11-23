define internal i32 @tiny(i32 %x) {
  %r = xor i32 %x, 1
  ret i32 %r
}
define internal i32 @large(i32 %x) {
  %v0 = add i32 %x, 1
  %v1 = add i32 %v0, 1
  %v2 = add i32 %v1, 1
  %v3 = add i32 %v2, 1
  %v4 = add i32 %v3, 1
  %v5 = add i32 %v4, 1
  %v6 = add i32 %v5, 1
  %v7 = add i32 %v6, 1
  %v8 = add i32 %v7, 1
  %v9 = add i32 %v8, 1
  %v10 = add i32 %v9, 1
  %v11 = add i32 %v10, 1
  %v12 = add i32 %v11, 1
  %v13 = add i32 %v12, 1
  %v14 = add i32 %v13, 1
  %v15 = add i32 %v14, 1
  %v16 = add i32 %v15, 1
  %v17 = add i32 %v16, 1
  %v18 = add i32 %v17, 1
  %v19 = add i32 %v18, 1
  %v20 = add i32 %v19, 1
  %v21 = add i32 %v20, 1
  %v22 = add i32 %v21, 1
  %v23 = add i32 %v22, 1
  %v24 = add i32 %v23, 1
  %v25 = add i32 %v24, 1
  %v26 = add i32 %v25, 1
  %v27 = add i32 %v26, 1
  %v28 = add i32 %v27, 1
  %v29 = add i32 %v28, 1
  %v30 = add i32 %v29, 1
  %v31 = add i32 %v30, 1
  %v32 = add i32 %v31, 1
  %v33 = add i32 %v32, 1

  ret i32 %v33
}
define dso_local i32 @run(ptr %p, i1 %b1, i1 %b2, i1 %b3, i1 %b4, i1 %b5, i1 %b6, i1 %b7) {
  %c = load i8, ptr %p
  %ok = icmp ne i8 %c, 0
  br i1 %ok, label %step1, label %hot
step1:
  br i1 %b1, label %step2, label %hot
step2:
  br i1 %b2, label %step3, label %hot
step3:
  br i1 %b3, label %step4, label %hot
step4:
  br i1 %b4, label %step5, label %hot
step5:
  br i1 %b5, label %step6, label %hot
step6:
  br i1 %b6, label %step7, label %hot
step7:
  br i1 %b7, label %cold, label %hot
hot:
  %h = call i32 @tiny(i32 11)
  ret i32 %h
cold:
  %s = call i32 @tiny(i32 17)
  %b = call i32 @large(i32 %s)
  ret i32 %b
}
