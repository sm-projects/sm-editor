smeditor: smeditor.c
	$(CC) smeditor.c -o smeditor -Wall -Wextra -pedantic -ggdb -std=c99

clean:
	rm  -rf smeditor

show:
	firefox  https://viewsourcecode.org/snaptoken/kilo/01.setup.html &

git-push:
	git push origin
