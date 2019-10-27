#define TGVOIP_USE_CALLBACK_AUDIO_IO

#include <iostream>
#include <fstream>

#include <tgvoip/VoIPController.h>
#include <tgvoip/VoIPServerConfig.h>
#include <tgvoip/json11.hpp>
#include <tgvoip/threading.h>
//#include <opus/opus.h>

int char_to_int(char input)
{
    if(input >= '0' && input <= '9')
        return input - '0';
    if(input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if(input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    return 0;
}

void hexstr_to_bin(const char* src, char* target)
{
    while(*src && src[1])
    {
        *(target++) = char_to_int(*src)*16 + char_to_int(src[1]);
        src += 2;
    }
}

bool audio_send = false;
FILE* input_file = NULL;
FILE* output_file = NULL;

size_t readInput;
size_t readOutput;

tgvoip::Mutex input_mutex;
tgvoip::Mutex output_mutex;

bool stream_end = false;

void send_frame(int16_t *data, size_t size)
{
    tgvoip::MutexGuard m(input_mutex);
    if(audio_send)
    {
        
        if ((readInput = fread(data, sizeof(int16_t), size, input_file)) != size)
        {
            audio_send = false;
            stream_end = true;
        }
    }
}


void recv_frame(int16_t *data, size_t size)
{
    tgvoip::MutexGuard m(output_mutex);
    
    if(size == 0)
    {
        return;
    }
    
    if (fwrite(data, sizeof(int16_t), size, output_file) != size){}
}

int main(int argc, char** argv)
{
    char* encrypt_key = NULL;
    char* ain_path = NULL;
    char* aout_path = NULL;
    char* cfg_path = NULL;
    uint8_t role = 0; // 0 - caller 1 - callee
    
    bool stop_argr = false;
    for(;;)
    {
        switch(getopt(argc, argv, "k:i:o:c:r:"))
        {
            case 'k':
                printf("key: %s\n", optarg);
                encrypt_key = optarg;
                break;
                
            case 'i':
                printf("input: %s\n", optarg);
                ain_path = optarg;
                break;
                
            case 'o':
                printf("output: %s\n", optarg);
                aout_path = optarg;
                break;
                
            case 'c':
                printf("config: %s\n", optarg);
                cfg_path = optarg;
                break;
                
            case 'r':
                printf("role: %s\n", optarg);
                if(memcmp(optarg, "caller", 6) == 0)
                {
                    role = 0;
                }
                else
                {
                    role = 1;
                }
                break;
                
            case '?':
                printf("undefined option: %c\n", optopt);
                break;
            case -1:
                stop_argr = true;
                break;
        }
        
        if(stop_argr)
        {
            break;
        }
    }
    
    std::string ip_str(argv[optind]);
    //7 is the minimum ip addr len (1.1.1.1)
    size_t separator_idx = ip_str.find(':', 7);
    
    auto id = std::stol("2394558673496"); //makes no difference, leave hardcoded for now
    auto ipv4 = tgvoip::IPv4Address(ip_str.substr(0, separator_idx));
    auto port = std::stol(ip_str.substr(separator_idx + 1));
    auto ipv6 = tgvoip::IPv6Address("0:0:0:0:0:0");
    
    char peer_tag[16];
    hexstr_to_bin(argv[optind + 1], peer_tag);
    
    ifstream cfg_strm { cfg_path };
    std::string cfg_str { istreambuf_iterator<char>(cfg_strm), istreambuf_iterator<char>() };
    
    std::string jerr;
    json11::Json cfg_obj = json11::Json::parse(cfg_str, jerr);
    if(!jerr.empty())
    {
        return 1;
    }
    
    bool use_tcp = (cfg_obj["use_tcp"].bool_value()) || (cfg_obj["force_tcp"].bool_value());
    auto ep = tgvoip::Endpoint(id, port, ipv4, ipv6, use_tcp ? tgvoip::Endpoint::Type::TCP_RELAY : tgvoip::Endpoint::Type::UDP_RELAY, (unsigned char*)peer_tag);
    auto endpoints = std::vector<tgvoip::Endpoint>();
    endpoints.push_back(ep);
    
    char encrypt_key_bin[256];
    hexstr_to_bin(encrypt_key, encrypt_key_bin);
    
    tgvoip::ServerConfig::GetSharedInstance()->Update(cfg_str);
    tgvoip::VoIPController* controller = new tgvoip::VoIPController;
    tgvoip::VoIPController::Callbacks callbacks;
    callbacks.connectionStateChanged = NULL;
    callbacks.signalBarCountChanged = NULL;
    callbacks.groupCallKeySent = NULL;
    callbacks.groupCallKeyReceived = NULL;
    callbacks.upgradeToGroupCallRequested = NULL;
    controller->SetCallbacks(callbacks);
    
    audio_send = true;
    
    std::string decoder_cmd = "nohup ffmpeg -i ";
    if(role == 0)
    {
        decoder_cmd = decoder_cmd + ain_path + " -vn -f s16le -ac 2 -ar 48000 -acodec pcm_s16le temp_out_caller.raw -y";
    }
    else
    {
        decoder_cmd = decoder_cmd + ain_path + " -vn -f s16le -ac 2 -ar 48000 -acodec pcm_s16le temp_out_callee.raw -y";
    }
    system(decoder_cmd.c_str());
    
    if(role == 0)
    {
        input_file = fopen("temp_out_caller.raw", "rb");
        output_file = fopen("temp_in_caller.raw", "wb");
    }
    else
    {
        input_file = fopen("temp_out_callee.raw", "rb");
        output_file = fopen("temp_in_callee.raw", "wb");
    }
    
    controller->SetAudioDataCallbacks(send_frame, recv_frame);
    
    tgvoip::VoIPController::Config cfg;
    cfg.dataSaving = tgvoip::DATA_SAVING_NEVER;
    cfg.enableAEC = false; //cfg_obj["use_system_aec"].bool_value(); TODO: currently corrupts data
    cfg.enableNS = cfg_obj["use_system_ns"].bool_value();
    cfg.enableAGC = cfg_obj["use_ios_vpio_agc"].bool_value();
    cfg.initTimeout = 5; //contest rule
    cfg.recvTimeout = 10;
    controller->SetRemoteEndpoints(endpoints, false, controller->GetConnectionMaxLayer());
    controller->SetConfig(cfg);
    
    if(role == 0)
    {
        controller->SetEncryptionKey(encrypt_key_bin, true);
    }
    else
    {
        controller->SetEncryptionKey(encrypt_key_bin, false);
    }
    
    controller->Start();
    controller->Connect();
    
    while(true)
    {
        if(stream_end)
        {
            usleep((useconds_t)(3*1000000.0));
            break;
        }
        else
        {
            usleep((useconds_t)(1*1000000.0));
        }
    }
    
    fclose(output_file);
    fclose(input_file);
    
    std::string encoder_cmd = "nohup ffmpeg -y -vn -f s16le -ac 2 -ar 48000 -acodec pcm_s16le -i ";
    std::string eout_params = " -acodec libopus -b:a 64000 -vbr off -compression_level 0 -frame_duration 60 -ac 1 -ar 48000 -vn -af \"silenceremove=start_periods=1:start_duration=1:start_threshold=-100dB:detection=peak,aformat=dblp,areverse,silenceremove=start_periods=1:start_duration=1:start_threshold=-100dB:detection=peak,aformat=dblp,areverse\" ";
    
    if(role == 0)
    {
        encoder_cmd = encoder_cmd + "temp_in_caller.raw" + eout_params + aout_path;
    }
    else
    {
        encoder_cmd = encoder_cmd + "temp_in_callee.raw" + eout_params + aout_path;
    }
    
    system(encoder_cmd.c_str());
    
    if(role == 0)
    {
        remove("temp_out_caller.raw");
        remove("temp_in_caller.raw");
    }
    else
    {
        remove("temp_out_callee.raw");
        remove("temp_in_callee.raw");
    }
    
    std::cout << "Log: " << controller->GetDebugLog() << std::endl;
    return 0;
}
