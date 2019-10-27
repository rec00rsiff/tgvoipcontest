#include <iostream>
#include <unistd.h>
#include <string>

int main(int argc, char** argv)
{
    FILE *f; //command output
    
    std::string encoder_cmd1 = "nohup ffmpeg -y -i ";
    encoder_cmd1 = encoder_cmd1 + std::string(argv[1]) + " -ac 1 -f wav -ar 16000 rate_temp1.wav";
    
    std::string encoder_cmd2 = "nohup ffmpeg -y -i ";
    encoder_cmd2 = encoder_cmd2 + std::string(argv[2]) + " -ac 1 -f wav -ar 16000 rate_temp2.wav";
    
    system(encoder_cmd1.c_str());
    system(encoder_cmd2.c_str());
    
    std::string cmd = "./pesq +16000 " + std::string("rate_temp1.wav ") + std::string("rate_temp2.wav");
    f = popen(cmd.c_str(), "r");
    
    if (f == 0)
    {
        return 1;
    }
    
    int line_size = 100;
    char line[line_size];
    std::string result;
    
    while (fgets(line, line_size, f))
    {
        result += line;
    }
    std::cout << result.substr(result.length() - 6) << std::endl;
    
    pclose(f);
    remove("rate_temp1.wav");
    remove("rate_temp2.wav");
    
    return 0;
}
