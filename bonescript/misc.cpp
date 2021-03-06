#define BUILDING_NODE_EXTENSION
#include <v8.h>
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#define PRINTF

using namespace std;
using namespace node;
using namespace v8;


extern "C" void init(Handle<Object>);
static void pollpri_event(EV_P_ ev_io * req, int revents);

#define QUOTE(name, ...) #name
#define STR(macro) QUOTE(macro)
#define TEST_EV_DEFAULT_NAME STR(EV_DEFAULT_)

class Pollpri: ObjectWrap {
private:
    int fd, epfd;
    char *path;
public:
    ev_io event_watcher, *ew;

    static Persistent<FunctionTemplate> ct;
    
    static void Init(Handle<Object> target) {
        PRINTF("Entering Init\n");
        PRINTF("EV_DEFAULT_ = %s\n", TEST_EV_DEFAULT_NAME);
        HandleScope scope;
        
        Local<FunctionTemplate> t = FunctionTemplate::New(New);
        ct = Persistent<FunctionTemplate>::New(t);
        ct->InstanceTemplate()->SetInternalFieldCount(1);
        ct->SetClassName(String::NewSymbol("Pollpri"));
        target->Set(String::NewSymbol("Pollpri"), ct->GetFunction());
        
        target->Set(String::NewSymbol("delay"), 
            FunctionTemplate::New(delay)->GetFunction());

        PRINTF("Leaving Init\n");
    }
    
    Pollpri() {
        PRINTF("Entering Pollpri constructor\n");
        epfd = 0;
        fd = 0;
        ev_init(&event_watcher, pollpri_event);
        event_watcher.data = this;
        ew = &event_watcher;
    }
    
    ~Pollpri() {
        PRINTF("Entering Pollpri destructor\n");
        if(epfd) close(epfd);
        if(fd) close(fd);
        ev_io_stop(EV_DEFAULT_ &event_watcher);
    }

    static Handle<Value> delay(const Arguments& args) {
        PRINTF("Entered delay\n");
        HandleScope scope;
        const char *usage = "usage: new delay(duration)";
        if((args.Length() != 1) || !args[0]->IsNumber()) {
            return ThrowException(Exception::Error(String::New(usage)));
        }
        int duration = args[0]->NumberValue();
        usleep(duration*1000);
        return(Undefined());
    }

    static Handle<Value> New(const Arguments& args) {
        PRINTF("Entered New\n");
        HandleScope scope;
        const char *usage = "usage: new Pollpri(path)";
        if(!args.IsConstructCall() || args.Length() != 1) {
            return ThrowException(Exception::Error(String::New(usage)));
        }
        String::Utf8Value path(args[0]);
        Pollpri *p = new Pollpri();
        p->Wrap(args.This());
        p->path = (char *)malloc(path.length() + 1);
        strncpy(p->path, *path, path.length() + 1);
        
        // Open file to watch
        int fd = open(p->path, O_RDWR | O_NONBLOCK);
        PRINTF("open(%s) returned %d: %s\n", p->path, fd, strerror(errno));
        
        // Create epoll event
        int epfd = epoll_create(1);
        PRINTF("epoll_create(1) returned %d: %s\n", epfd, strerror(errno));
        struct epoll_event ev;
        ev.events = EPOLLPRI;
        ev.data.fd = fd;
        int n = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
        PRINTF("epoll_ctl(%d) returned %d (%d): %s\n", fd, n, epfd, strerror(errno));

        // Read initial state
        int m = 0;
        char buf[64];
        m = lseek(fd, 0, SEEK_SET);
        PRINTF("seek(%d) %d bytes: %s\n", fd, m, strerror(errno));
        m = read(fd, &buf, 63);
        buf[m] = 0;
        PRINTF("read(%d) %d bytes (%s): %s\n", fd, m, buf, strerror(errno));

        // Setup event watcher
        ev_io_set(p->ew, epfd, EV_READ);
        ev_io_start(EV_DEFAULT_ p->ew);
        
        p->epfd = epfd;
        p->fd = fd;        
        
        PRINTF("Leaving New\n");
        return(args.This());
    }

    void Event(Pollpri * p, int revents) {
        HandleScope scope;
        PRINTF("fd = %d, epfd = %d, revents = 0x%0x\n", fd, epfd, revents);
        if(revents != EV_READ) {
            return;
        }
        
        int m = 0;
        char buf[64];
        m = lseek(fd, 0, SEEK_SET);
        PRINTF("seek(%d) %d bytes: %s\n", fd, m, strerror(errno));
        m = read(fd, &buf, 63);
        buf[m] = 0;
        PRINTF("read(%d) %d bytes (%s): %s\n", fd, m, buf, strerror(errno));

        Local<Value> emit_v = handle_->Get(String::NewSymbol("emit"));
        assert(emit_v->IsFunction());
        Local<Function> emit_f = emit_v.As<Function>();
        
        Handle<Value> argv[2];
        argv[0] = String::New("edge");
        argv[1] = String::New(buf);
        
        TryCatch tc;
        
        emit_f->Call(handle_, 2, argv);

        if(tc.HasCaught()) {
            FatalException(tc);
        }
    }
    
    static void pollpri_event(EV_P_ ev_io * req, int revents) {
        PRINTF("Entered pollpri_event\n");
        Pollpri *p = static_cast<Pollpri*>(req->data);
        p->Event(p, revents);
        PRINTF("Leaving pollpri_event\n");
    }
};

Persistent<FunctionTemplate> Pollpri::ct;

extern "C" {
    void init(Handle<Object> target) {
        PRINTF("Calling Init\n");
        Pollpri::Init(target);
    }
    
    NODE_MODULE(pollpri, init)
}
