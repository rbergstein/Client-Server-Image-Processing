#include "client.h"

#define PORT 6872
#define BUFFER_SIZE 1024 
char rotation_angle;

int send_file(int socket, const char *filename) {
    printf("--in send_file func--\n");
    printf("--file name: %s--\n", filename);
    // Open the file
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        perror("can't open file");
        return -1;
    }

    fseek(f, 0, SEEK_END); // set file pointer to end of file
    int file_size = ftell(f); // get size of file
    fseek(f, 0, SEEK_SET); // set file pointer back to start

    packet_t packet;                //moved packet stuff from main to this function
    if (rotation_angle == 180) {
        packet = (packet_t) {
            .operation = IMG_OP_ROTATE,
            .flags = IMG_FLAG_ROTATE_180, 
            .size = htonl(file_size)};
    }
    else {
        packet = (packet_t) {
            .operation = IMG_OP_ROTATE,
            .flags = IMG_FLAG_ROTATE_270, 
            .size = htonl(file_size)};      
    }    
    char *serializedData = serializePacket(&packet);
    int ret = send(socket, serializedData, packet.size, 0);
    if (ret == -1) {
        perror("packet send error");
        fclose(f);
        return -1;
    }

    // Send the file data
    char pack_buf[PACKETSZ];
    memset(pack_buf, 0, PACKETSZ);
    size_t bytes_read;

    while (bytes_read < packet.size) {
        ret = send(socket, pack_buf + bytes_read, packet.size - bytes_read, 0);
        if (ret == -1) {
            perror("file data send error");
            fclose(f);
            return -1;
        }
        bytes_read += ret;
    }

    fclose(f);
    free(serializedData);
}

int receive_file(int socket, const char *filename) {
    printf("--in receive_file func--\n");
    // Open the file
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        perror("can't open file");
        return -1;
    }
    // Receive response packet
    packet_t packet;
    int ret = recv(socket, &packet, sizeof(packet), 0);
    if (ret == -1) {
        perror("receive packet error");
    }
    // Receive the file data
    char pack_buf[PACKETSZ];
    memset(pack_buf, 0, PACKETSZ);
    size_t bytes_received;
    int temp_size = packet.size;

    while (temp_size > 0) {
        bytes_received = recv(socket, pack_buf, sizeof(pack_buf), 0);
        if (bytes_received == 0) {
            fclose(f);
            return -1;
        }
        if (bytes_received == -1) {
            perror("file data receive error");
            fclose(f);
            return -1;
        }

        // Write the data to the file
        fwrite(pack_buf, 1, bytes_received, f);

        temp_size -= bytes_received;
    }
    
    fclose(f);

    return 0;
}

int main(int argc, char* argv[]) {
    if(argc != 4){
        fprintf(stderr, "Usage: ./client File_Path_to_images File_Path_to_output_dir Rotation_angle. \n");
        return 1;
    }
    char* path_to_images = argv[1];
    char* output_dir = argv[2];
    rotation_angle = atoi(argv[3]);
    
    // Set up socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // create socket to establish connection
    if(sockfd == -1) {
        perror("Failed to set up socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // server IP, since the server is on same machine, use localhost IP
    servaddr.sin_port = htons(PORT); // Port the server is listening on

    // Connect the socket
    int ret = connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)); // establish connection to server
    if(ret == -1)
        perror("Failed to connect socket");

    // Read the directory for all the images to rotate
    DIR *dir = opendir(path_to_images);
    struct dirent *entry;

    if (dir == NULL) {
        perror("Failed to open directory");
        exit(EXIT_FAILURE);
    }

    request_t req_queue[MAX_QUEUE_LEN];
    int index_counter = 0;

    while ((entry = readdir(dir)) != NULL) { 
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        const char* file_ext = strrchr(entry->d_name, '.');
        if (file_ext && strcmp(file_ext, ".png") == 0) {
            if (index_counter < MAX_QUEUE_LEN) {
                req_queue[index_counter].file_name = strdup(entry->d_name); //memory allocation for file_name
                req_queue[index_counter].rotation_angle = rotation_angle;
                index_counter++;
            }
        }
    }
    // Send the image data to the server

    while (index_counter > 0) {
        //pop from queue
        index_counter--;
        char *f_name = req_queue[index_counter].file_name;
        // Send a packet with the IMG_FLAG_ROTATE_XXX message header desired rotation Angle, Image size, and data.
        
        char *f_path[BUFFER_SIZE];
        sprintf(f_path, "%s/%s", path_to_images, f_name);
        send_file(sockfd, f_path);

        // Receive the processed image and write it to output_dir
        char *out_path[BUFFER_SIZE];
        sprintf(out_path, "%s/%s", output_dir, f_name);
        receive_file(sockfd, out_path);

        free(req_queue[index_counter].file_name);
    }

    // Terminate the connection once all images have been processed (Send ‘terminate’ message through socket)
    packet_t terminate_packet = {
                .operation = IMG_OP_EXIT,
                .size = 0
    };

    char *serializedTerminate = serializePacket(&terminate_packet);
    ret = send(sockfd, serializedTerminate, terminate_packet.size, 0);
    if (ret == -1) {
        perror("packet send error");
    }
    free(serializedTerminate);
    // Release any resources (Close to connection)    
    close(sockfd);
    fprintf(stdout, "Client exiting...\n");
    return 0;
}
