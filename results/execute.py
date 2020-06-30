import os, sys


def main():
    
    detector_types = ["HARRIS", "FAST", "BRISK", "ORB", "AKAZE", "SIFT"]
    descriptor_types = ["BRIEF", "ORB", "FREAK", "AKAZE", "SIFT"]
    
    if len(sys.argv) == 2:
        result_file = sys.argv[1]
    else: 
        result_file = "{}/results.csv".format(os.getcwd())
        
    for detector in detector_types:
        for descriptor in descriptor_types:
            cmd = "../build/results {} {} {} ".format(detector, descriptor, result_file)
            os.system(cmd)
            # print(cmd)

if __name__ == "__main__":
    main()