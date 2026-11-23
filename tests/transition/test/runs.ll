; The late pass receives the read-only transition table from the early pass.
@rows = private constant [512 x i16] zeroinitializer, !parser.transition.rows !0
!0 = !{i32 4}
define i64 @scan(ptr %start, ptr %end, i32 %seed) {
entry:
  %empty = icmp eq ptr %start, %end
  br i1 %empty, label %emptyret, label %begin
emptyret:
  ret i64 0
begin:
  %first = and i32 %seed, 3
  br label %head
head:
  %p = phi ptr [%start, %begin], [%next, %latch]
  %state = phi i32 [%first, %begin], [%after, %latch]
  %byte = load i8, ptr %p
  %idx = shl i32 %state, 7
  %addr = getelementptr [512 x i16], ptr @rows, i64 0, i32 %idx
  %tag = load i16, ptr %addr
  %n = zext i8 %byte to i32
  %base = add i32 %state, 63
  %delta = sub i32 %n, %base
  %range = icmp ult i32 %delta, 27
  %known = icmp eq i16 %tag, 0
  %keep = and i1 %range, %known
  br i1 %keep, label %ordinary, label %action
ordinary:
  br label %latch
action:
  %stop = icmp eq i8 %byte, 10
  br i1 %stop, label %out, label %change
change:
  %sum = add i32 %n, %state
  %changed = and i32 %sum, 3
  br label %latch
latch:
  %after = phi i32 [%state, %ordinary], [%changed, %change]
  %next = getelementptr i8, ptr %p, i64 1
  %done = icmp eq ptr %next, %end
  br i1 %done, label %out, label %head
out:
  %cs = phi i32 [%state, %action], [%after, %latch]
  %last = ptrtoint ptr %p to i64
  %firstaddr = ptrtoint ptr %start to i64
  %offset = sub i64 %last, %firstaddr
  %pos = shl i64 %offset, 8
  %low = zext i32 %cs to i64
  %result = or i64 %pos, %low
  ret i64 %result
}
