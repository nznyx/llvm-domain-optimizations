define i32 @nonzero(i32 %error) {
entry:
  %c = icmp ne i32 %error, 0
  br i1 %c, label %bad, label %good
bad:
  ret i32 7
good:
  ret i32 3
}

define i32 @zero(i32 %error) {
entry:
  %c = icmp eq i32 %error, 0
  br i1 %c, label %good, label %bad
good:
  ret i32 3
bad:
  ret i32 7
}

define i32 @ordinary(i32 %count) {
entry:
  %c = icmp ne i32 %count, 0
  br i1 %c, label %yes, label %no
yes:
  ret i32 7
no:
  ret i32 3
}

define i32 @prior(i32 %error) {
entry:
  %c = icmp ne i32 %error, 0
  br i1 %c, label %bad, label %good, !prof !0
bad:
  ret i32 7
good:
  ret i32 3
}
!0 = !{!"branch_weights", i32 123, i32 456}

declare i32 @get_error()
declare i1 @has_error()

define i32 @uppercase(i32 %ERROR) {
entry:
  %c = icmp ne i32 %ERROR, 0
  br i1 %c, label %bad, label %good
bad:
  ret i32 7
good:
  ret i32 3
}

define i32 @called_code() {
entry:
  %v = call i32 @get_error()
  %c = icmp eq i32 %v, 7
  br i1 %c, label %bad, label %good
bad:
  ret i32 7
good:
  ret i32 3
}

define i32 @boolean_true() {
entry:
  %v = call i1 @has_error()
  %c = icmp eq i1 true, %v
  br i1 %c, label %bad, label %good
bad:
  ret i32 7
good:
  ret i32 3
}

define i32 @reversed_xor(i32 %error) {
entry:
  %v = icmp ne i32 %error, 0
  %c = xor i1 true, %v
  br i1 %c, label %good, label %bad
good:
  ret i32 3
bad:
  ret i32 7
}

define i32 @null_on_left(ptr %error) {
entry:
  %c = icmp eq ptr null, %error
  br i1 %c, label %good, label %bad
good:
  ret i32 3
bad:
  ret i32 7
}

define i32 @truncated(i32 %error) {
entry:
  %v = trunc i32 %error to i8
  %c = icmp ne i8 %v, 0
  br i1 %c, label %yes, label %no
yes:
  ret i32 7
no:
  ret i32 3
}
