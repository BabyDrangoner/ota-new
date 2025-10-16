#include "../sherry/log.h"
#include "../sherry/http/http_buffer.h"

#include <memory.h>


std::shared_ptr<sherry::HttpBuffer> buffer(new sherry::HttpBuffer(20));

void test_read_write(){
    buffer->print_infos();

    char data[] = {'a', 'b'};
    buffer->write(data, 2);
    buffer->print_infos();

    auto _data = buffer->read_by_size(1);
    buffer->print_infos();
    
}

void test_ring(){

    std::string data(20, 'a');
    std::cout << data << std::endl;

    buffer->write(data.data(), data.size());
    buffer->print_infos();

    char data1[] = {'b', 'c'};
    int i = 0;
    while(!buffer->write(data1, 2)){
        std::cout << "write false" << std::endl;
        buffer->print_infos();
        if(i == 2){
            auto ret = buffer->read_by_size(2);
            std::cout << "read size = 2" << std::endl;
            buffer->print_infos();

        }
        ++i;

    }

    buffer->print_infos();

    std::cout << "read all:" << std::endl;
    buffer->read_by_size(20);
    buffer->print_infos();

}

int main(){
    test_ring();
    return 0;
}