# tells make to run html-local when the user asks for 'all'.
all: doc
 
doc:
	rm -rf html/
	doxygen usb_moded-doxygen.conf 1>doxygen.out # stdout=noise
 
clean-local:
	-rm -rf html/ doxygen.out
 
