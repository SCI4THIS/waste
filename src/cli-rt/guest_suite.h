/* Native batch capability adapter. Guest argv cannot select host root,
 * executable, policy, or report paths. The trusted companion owns isolation. */
#include <sys/wait.h>
#include <signal.h>

static int session_suite_reply(native_host_io_state *io, unsigned code,
                               const uint8_t *out, size_t out_length,
                               const uint8_t *json, size_t json_length) {
    if (out_length > 16u * 1024u * 1024u - 16u ||
        json_length > 16u * 1024u * 1024u - 16u - out_length) return 0;
    size_t length = 16 + out_length + json_length;
    uint8_t *reply = malloc(length);
    if (!reply) return 0;
    uint32_t words[4] = {1, code, (uint32_t)out_length, (uint32_t)json_length};
    for (unsigned i = 0; i < 4; i++) for (unsigned j = 0; j < 4; j++)
        reply[i*4+j] = (uint8_t)(words[i] >> (j*8));
    if (out_length) memcpy(reply+16, out, out_length);
    if (json_length) memcpy(reply+16+out_length, json, json_length);
    free(io->data); io->data = reply; io->data_len = length; io->result = 1;
    return 1;
}

static int session_test_suite(cli_guest_session *session, exec_error *error) {
    native_host_io_state *io = &session->store.host_io;
    char executable[PATH_MAX], baseline[PATH_MAX+80], root_arg[PATH_MAX+32];
    ssize_t n = readlink("/proc/self/exe", executable, sizeof(executable)-1);
    if (n < 0) return 0;
    executable[n] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash) return 0;
    int sanitized = strstr(slash, "-sanitize") != NULL;
    *slash = '\0';
    snprintf(baseline, sizeof(baseline), "--expected-failures=%s/native-corpus-expected-failures.txt", executable);
    size_t directory_length = strlen(executable);
    const char *name = sanitized ? "/waste-test-sanitize" : "/waste-test";
    if (directory_length + strlen(name) >= sizeof(executable)) return 0;
    memcpy(executable + directory_length, name, strlen(name)+1);
    if (session->suite_root_fd < 0) return 0;
    int root_fd = session->suite_root_fd;
    snprintf(root_arg, sizeof(root_arg), "--vfs-root=/proc/self/fd/%d", root_fd);
    char report_path[] = "/tmp/waste-guest-suite-XXXXXX";
    int report_fd = mkstemp(report_path);
    FILE *output = tmpfile();
    if (report_fd < 0 || !output) {
        if (report_fd >= 0) { close(report_fd); unlink(report_path); }
        if (output) fclose(output);
        return 0;
    }
    close(report_fd);
    char report_arg[128];
    snprintf(report_arg, sizeof(report_arg), "--results=%s", report_path);
    char *argv[71] = {executable, root_arg, baseline, report_arg, "--jobs=2"};
    unsigned argc = 5; int json_output = 0;
    for (size_t i = 4; i < io->data_len;) {
        char *arg = (char *)io->data+i;
        if (!strcmp(arg, "--json")) json_output = 1;
        else argv[argc++] = arg;
        i += strlen(arg)+1;
    }
    argv[argc] = NULL;
    fflush(NULL);
    pid_t child = fork();
    if (!child) {
        (void)setpgid(0, 0);
        int flags = fcntl(root_fd, F_GETFD);
        if (flags < 0 || fcntl(root_fd, F_SETFD, flags & ~FD_CLOEXEC)) _exit(2);
        if (dup2(fileno(output), 1) < 0 || dup2(fileno(output), 2) < 0) _exit(2);
        execv(executable, argv);
        _exit(2);
    }
    int status = 0, ok = child > 0, cancelled = 0;
    if (ok) {
        (void)setpgid(child, child);
        uint64_t stopped_at = 0;
        for (;;) {
            pid_t done = waitpid(child, &status, WNOHANG);
            if (done == child) break;
            if (done < 0 && errno != EINTR) { ok = 0; break; }
            if (!stopped_at && session_control(session) != EXEC_STOP_NONE) {
                stopped_at = host_now(); cancelled = 1; (void)kill(-child, SIGINT);
            }
            if (stopped_at && host_now()-stopped_at > 1000000000u)
                (void)kill(-child, SIGKILL);
            struct pollfd input = {session->input_eof ? -1 : STDIN_FILENO, POLLIN, 0};
            if (poll(&input, 1, 10) > 0 && (input.revents & (POLLIN | POLLHUP))) {
                uint8_t bytes[1024];
                ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
                if (!count) {
                    session->input_eof = 1;
                    (void)posix_kernel_terminal_signal_eof(session->store.kernel, 0);
                }
                unsigned retained = 0;
                for (ssize_t i = 0; i < count; i++) {
                    if (bytes[i] == 3) {
                        if (!stopped_at) { stopped_at = host_now(); cancelled = 1; (void)kill(-child, SIGINT); }
                    } else bytes[retained++] = bytes[i];
                }
                if (retained) {
                    (void)posix_kernel_terminal_enqueue(session->store.kernel, 0, bytes, (int)retained);
                    session->input_bytes += retained;
                }
            }
        }
    }
    size_t json_length = 0, out_length = 0;
    uint8_t *json = session_read_file(report_path, 16u*1024u*1024u, &json_length);
    unlink(report_path);
    if (cancelled && !json_length) {
        const char early[] = "{\"summary\":{\"pass\":0,\"fail\":0,\"xfail\":0,\"xpass\":0,\"skip\":0,\"failures\":[],\"unexpectedPasses\":[]},\"tests\":[],\"exitCode\":130,\"cancelledBeforeEnumeration\":true}\n";
        free(json);
        json_length = sizeof(early)-1;
        json = malloc(json_length);
        if (json) memcpy(json, early, json_length);
        else json_length = 0;
    }
    long out_size = -1;
    if (!fseek(output, 0, SEEK_END)) out_size = ftell(output);
    uint8_t *out = NULL;
    if (out_size >= 0 && out_size <= 16*1024*1024 && !fseek(output, 0, SEEK_SET)) {
        out = malloc((size_t)out_size+1);
        if (out) out_length = fread(out, 1, (size_t)out_size, output);
    }
    fclose(output);
    unsigned code = cancelled ? 130 : ok && WIFEXITED(status) ? (unsigned)WEXITSTATUS(status) : 2;
    if (code != 0 && code != 1 && code != 2 && code != 130) code = 2;
    const uint8_t *text = json_output && json_length ? json : out;
    size_t text_length = json_output && json_length ? json_length : out_length;
    int replied = session_suite_reply(io, code, text, text_length, json, json_length);
    free(json); free(out);
    if (!replied) snprintf(error->message, sizeof(error->message), "cannot encode bounded batch response");
    return replied;
}
