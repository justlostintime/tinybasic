clear
5 a=0
10 if a > 10000 goto 1000
20 if (a - (a/100 *100)) = 0 then print a
30 a=a+1
40 goto 10
1000 print "done : ";a
1010 goto 5
run


clear
5 a=0
10 if a > 10000 goto 1000
30 a=a+1
40 goto 10
1000 print "done : ";a
1010 end
run

clear
5 a=0
10 if a > 10000 goto 1000
30 a=a+1
40 goto 10
1000 goto 5
run

