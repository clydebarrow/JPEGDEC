CXXFLAGS = -Wall -O2 -D__LINUX__

all: run

step_test: step_test.cpp ../../src/JPEGDEC.cpp ../../src/JPEGDEC.h ../../src/jpeg.inl
	$(CXX) $(CXXFLAGS) -o $@ step_test.cpp -lm

crop_test: crop_test.cpp ../../src/JPEGDEC.cpp ../../src/JPEGDEC.h ../../src/jpeg.inl
	$(CXX) $(CXXFLAGS) -o $@ crop_test.cpp -lm

images/.done: make_images.py
	python3 make_images.py images && touch $@

run: step_test crop_test images/.done
	./step_test images/*.jpg ../../demo.jpg ../../perf.jpg
	./crop_test images/*.jpg ../../demo.jpg ../../perf.jpg

clean:
	rm -rf step_test crop_test images
