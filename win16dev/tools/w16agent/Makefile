NAME = w16agent
include $(HOME)/win16dev/w16.mk

# install into the running VM (Windows restarts itself)
deploy: w16agent.exe
	w31x put w16agent.exe
	w16ctl UPDATE 'A:\W16AGENT.EXE'
