parameter = -I/usr/include/SDL2
lib = -lSDL2 -lSDL2_image -lSDL2_ttf
protect = -fno-stack-protector -z execstack -no-pie -g
dir = source/
all: Battery Communication Dashboard ICSim CVE-2022-33218

Battery: $(dir)Battery.c $(dir)cJSON.c
	@gcc $(dir)Battery.c $(dir)cJSON.c -o Battery $(lib) $(parameter)

Communication: $(dir)Communication.c $(dir)cJSON.c
	@gcc $(dir)Communication.c $(dir)cJSON.c -o Communication $(lib) $(parameter)

Dashboard: $(dir)Dashboard.c
	@gcc $(dir)Dashboard.c -o Dashboard $(lib) $(parameter)

ICSim: $(dir)ICSim.c
	@gcc $(dir)ICSim.c -o ICSim $(lib) $(parameter)

CVE-2022-33218: $(dir)CVE-2022-33218.c
	@gcc $(dir)CVE-2022-33218.c -g -o CVE-2022-33218 $(lib) $(parameter) $(protect) 
	@echo "make down"
	
clean:
	@rm -rf Battery Communication Dashboard ICSim Seatbelt CVE-2022-33218
	@echo "clean down"