@threshold = internal constant [4 x i8] [i8 48, i8 65, i8 97, i8 32], !parser.transition.rows !0
!0 = !{i32 4}

; A byte inside the state's range repeats the state. Other bytes change state.
; Stores on ordinary iterations are visible to the caller and may alias the input.
define i32 @scan(ptr %begin, ptr %end, i32 %seed, ptr %mark) {
entry:
  %empty = icmp eq ptr %begin,%end
  br i1 %empty,label %emptyret,label %init
emptyret:
  ret i32 %seed
init:
  %initial = and i32 %seed,3
  br label %head
head:
  %p = phi ptr [%begin,%init],[%next,%latch]
  %s = phi i32 [%initial,%init],[%after,%latch]
  %byte = load i8,ptr %p
  %at = getelementptr [4 x i8],ptr @threshold,i32 0,i32 %s
  %lo = load i8,ptr %at
  %delta = sub i8 %byte,%lo
  %ordinary = icmp ult i8 %delta,10
  br i1 %ordinary,label %take,label %action
take:
  store i32 %s,ptr %mark,align 1
  br label %latch
action:
  %v = zext i8 %byte to i32
  %u = add i32 %s,%v
  %changed = and i32 %u,3
  br label %latch
latch:
  %after = phi i32 [%s,%take],[%changed,%action]
  %next = getelementptr i8,ptr %p,i64 1
  %done = icmp eq ptr %next,%end
  br i1 %done,label %out,label %head
out:
  ret i32 %after
}

; The zero byte ends the stream. There is no accessible padding after it.
define i32 @sentinel(ptr %begin,i32 %seed) {
entry:
  %initial = and i32 %seed,3
  br label %head
head:
  %p = phi ptr [%begin,%entry],[%next,%latch]
  %s = phi i32 [%initial,%entry],[%after,%latch]
  %byte = load i8,ptr %p
  %stop = icmp eq i8 %byte,0
  br i1 %stop,label %out,label %class
class:
  %at = getelementptr [4 x i8],ptr @threshold,i32 0,i32 %s
  %lo = load i8,ptr %at
  %delta = sub i8 %byte,%lo
  %ordinary = icmp ult i8 %delta,10
  %v = zext i8 %byte to i32
  %candidate = xor i32 %s,%v
  %changed = and i32 %candidate,3
  %after = select i1 %ordinary,i32 %s,i32 %changed
  br label %latch
latch:
  %next = getelementptr i8,ptr %p,i64 1
  br label %head
out:
  ret i32 %s
}
