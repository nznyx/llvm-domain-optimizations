@lengths = internal constant [5 x i8] [i8 1,i8 2,i8 3,i8 4,i8 5]
@tokens = internal constant [6 x i32] [i32 101,i32 19,i32 32,i32 85,i32 11,i32 7]

@visits = global i32 0

define i32 @decode(i32 %state, i8 %byte) null_pointer_is_valid {
entry:
  %valid = icmp ult i32 %state, 5
  br i1 %valid, label %start, label %invalid
invalid:
  ret i32 -1
start:
  %visits = load volatile i32, ptr @visits
  %inc = add i32 %visits, 1
  store volatile i32 %inc, ptr @visits
  %idx = zext i32 %state to i64
  %slot = getelementptr [5 x i8], ptr @lengths, i64 0, i64 %idx
  %size = load i8, ptr %slot
  br label %search
search:
  %i = phi i8 [0,%start], [%next,%advance]
  %remaining = icmp ult i8 %i,%size
  br i1 %remaining, label %test, label %miss
test:
  %ch = add i8 %i,65
  %hit = icmp eq i8 %byte,%ch
  br i1 %hit, label %found, label %advance
advance:
  %next = add i8 %i,1
  br label %search
found:
  %n = zext i8 %i to i32
  br label %done
miss:
  br label %done
done:
  %result = phi i32 [%n,%found], [5,%miss]
  %at = getelementptr [6 x i32], ptr @tokens, i32 0, i32 %result
  %token = load i32, ptr %at
  ret i32 %token
}

@classes = internal constant [256 x i8] c"\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03\00\01\02\03"
@parents = internal constant [6 x i8] [i8 0,i8 0,i8 1,i8 2,i8 3,i8 4]
@accept = internal constant [9 x i8] [i8 12,i8 5,i8 4,i8 8,i8 9,i8 3,i8 1,i8 11,i8 2]

define i32 @step(i32 %state, i8 %byte) {
entry:
  %valid = icmp ult i32 %state, 6
  br i1 %valid, label %prepare, label %invalid
invalid:
  ret i32 -1
prepare:
  %byteIndex = zext i8 %byte to i64
  %slot = getelementptr [256 x i8], ptr @classes, i64 0, i64 %byteIndex
  %class = load i8, ptr %slot
  br label %search
search:
  %s = phi i32 [%state,%prepare], [%old,%retry]
  %c = phi i8 [%class,%prepare], [%other,%retry]
  %ci = zext i8 %c to i32
  %zero = icmp eq i32 %s,0
  %matches = icmp eq i32 %s,%ci
  %found = or i1 %zero,%matches
  br i1 %found, label %done, label %retry
retry:
  %parent = getelementptr [6 x i8], ptr @parents, i32 0,i32 %s
  %n = load i8,ptr %parent
  %old = zext i8 %n to i32
  %other = xor i8 %c,1
  br label %search
done:
  %offset = add i32 %s,%ci
  %p = getelementptr [9 x i8],ptr @accept,i32 0,i32 %offset
  %v = load i8,ptr %p
  %odd = icmp eq i8 %v,5
  br i1 %odd,label %tagged,label %raw
tagged:
  ret i32 50
raw:
  %r = zext i8 %v to i32
  ret i32 %r
}
