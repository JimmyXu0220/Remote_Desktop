#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef struct SampleData {
    int16_t value;      // The audio sample
    int ref_count;      // How many nodes share this sample
} SampleData;

// A track is a singly linked list of Nodes.
typedef struct Node {
    SampleData* sample_ptr;
    bool is_child;
    struct Node* next;
} Node;

// This is the "opaque" struct for the track.
typedef struct sound_seg {
    Node* head;         // start of linked list
    size_t length;      // how many samples in total
}Track;

SampleData* getSample(Node* node) {
    return node->sample_ptr;
}


// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){
    FILE *file = fopen(filename, "rb"); //read binery mode

    fseek(file, 44, SEEK_SET);  // Skip the WAV header (44 bytes) due to header

    size_t read_count = fread(dest, sizeof(int16_t), 80000, file); //16bits, mono, 8000Hz
    fclose(file);
}

// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    FILE *file = fopen(fname, "wb");
    uint8_t header[44] = {
        'R', 'I', 'F', 'F', 
        36 + len * 2, 0, 0, 0,
        'W', 'A', 'V', 'E', 
        'f', 'm', 't', ' ', 
        16, 0, 0, 0, 
        1, 0, //PCM
        1, 0, //mono
        0x40, 0x1F, 0, 0, // Sample rate = 8000 Hz
        0x80, 0x3E, 0, 0, //byte rate 16000
        2, 0, 
        16, 0, // 16-bit PCM mono
        'd', 'a', 't', 'a', 
        len * 2, 0, 0, 0 // Data size
    };
    fwrite(header, 1, 44, file);
    fwrite(src, sizeof(int16_t), len, file);

    fclose(file);
}

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    Track * new_track = (Track*)malloc(sizeof(Track));
    new_track->head = NULL;
    new_track->length = 0;
    return new_track;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    Node* curr = track->head;
    while (curr) {
        Node* next_node = curr->next;
        // Decrement ref_count; if 0, free the SampleData
        // curr->sample_ptr->ref_count--;
        // if (curr->is_child && curr->sample_ptr->ref_count == 0) {
        //     free(curr->sample_ptr);
        // }
        free(curr);
        curr = next_node;
    }
    free(track);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* track) {
    return track->length;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    Node* curr = track->head;
    for (size_t i = 0; i < pos && curr != NULL; i++) {
        curr = curr->next;
    }

    for (size_t i = 0; i < len && curr != NULL; i++) {
        dest[i] = curr->sample_ptr->value;
        curr = curr->next;
    }
}


// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    Track* t = (Track*)track;
    Node* curr = t->head;
    Node* prev = NULL;

    size_t idx = 0;
    // Move to position pos
    while (idx < pos && curr) {
        prev = curr;
        curr = curr->next;
        idx++;
    }

    for (size_t i = 0; i < len; i++) {
        // If we're off the end, create a new node
        if (curr == NULL) {
            // Allocate one block for Node + SampleData
            Node* new_node = (Node*)malloc(sizeof(Node) + sizeof(SampleData));
            new_node->is_child = false;

            // The sample is directly after the Node in memory
            SampleData* smp = (SampleData*)(new_node + 1);
            smp->value = 0;
            smp->ref_count = 1;

            new_node->sample_ptr = smp;
            new_node->next = NULL;

            if (!prev) {
                t->head = new_node;
            } else {
                prev->next = new_node;
            }
            curr = new_node;
            t->length++;
        }

        // Overwrite the existing sample
        SampleData* sample = getSample(curr);
        sample->value = src[i];

        prev = curr;
        curr = curr->next;
        idx++;
    }
}

bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    Node* prev = NULL;
    Node* curr = track->head;

    // Move curr to the node at position pos
    for (size_t i = 0; i < pos && curr != NULL; i++) {
        prev = curr;
        curr = curr->next;
    }

    // Check nodes in the range:
    // If a parent node (is_child == false) is shared (ref_count > 1), deletion fails.
    Node* temp_check = curr;
    for (size_t i = 0; i < len && temp_check != NULL; i++) {
        if (!temp_check->is_child && temp_check->sample_ptr->ref_count > 1) {
            return false;
        }
        temp_check = temp_check->next;
    }

    // Delete len nodes from the track
    for (size_t i = 0; i < len && curr != NULL; i++) {
        Node* temp = curr;
        curr = curr->next;

        // Decrement reference count
        temp->sample_ptr->ref_count--;
        // Only free sample_ptr if this is a child node
        // if (temp->is_child) {
        //     if (temp->sample_ptr->ref_count == 0) {
        //         free(temp->sample_ptr);
        //     }
        // }
        // Free the node itself. For parent nodes, the SampleData is part of the same block.
        free(temp);
        track->length--;
    }

    // Fix the links in the list
    if (prev == NULL) {
        track->head = curr;
    } else {
        prev->next = curr;
    }

    return true;
}

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad) {
    // 1. Compute ad's autocorrelation at zero delay
    double ref_corr = 0.0;
    Node* ad_ptr = ad->head;
    while (ad_ptr != NULL) {
        double val = (double)ad_ptr->sample_ptr->value;
        ref_corr += val * val;
        ad_ptr = ad_ptr->next;
    }

    if (ref_corr == 0.0) {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    // 2. Allocate result buffer
    size_t buffer_size = 256;
    char* result = malloc(buffer_size);
    if (!result) return NULL;
    result[0] = '\0';

    // 3. Sliding window on target
    for (size_t i = 0; i <= target->length - ad->length; i++) {
        Node* t_ptr = target->head;

        // move t_ptr to position i
        for (size_t skip = 0; skip < i && t_ptr != NULL; skip++) {
            t_ptr = t_ptr->next;
        }

        Node* a_ptr = ad->head;
        Node* match_ptr = t_ptr;
        double match_corr = 0.0;

        for (size_t j = 0; j < ad->length && match_ptr != NULL; j++) {
            double a_val = (double)a_ptr->sample_ptr->value;
            double t_val = (double)match_ptr->sample_ptr->value;
            match_corr += a_val * t_val;

            a_ptr = a_ptr->next;
            match_ptr = match_ptr->next;
        }

        double similarity = match_corr / ref_corr;

        if (similarity >= 0.95) {
            // Found a match
            char line[50];
            snprintf(line, sizeof(line), "%zu,%zu\n", i, i + ad->length - 1);

            // Ensure result has enough space
            if (strlen(result) + strlen(line) + 1 > buffer_size) {
                buffer_size *= 2;
                char* temp = realloc(result, buffer_size);
                if (!temp) {
                    free(result);
                    return NULL;
                }
                result = temp;
            }

            strcat(result, line);

            // Skip ahead past this match
            i += ad->length - 1;
        }
    }

    // 4. Clean up formatting
    if (result[0] == '\0') {
        result[0] = '\0';
    } else {
        size_t len = strlen(result);
        if (result[len - 1] == '\n') {
            result[len - 1] = '\0';
        }
    }

    return result;
}


// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
            struct sound_seg* dest_track,
            size_t destpos, size_t srcpos, size_t len) {

    Node* src_curr = src_track->head;
    for (size_t i = 0; i < srcpos && src_curr != NULL; i++) {
        src_curr = src_curr->next;
    }

    // 2. Build a chain of new nodes (child nodes) that reference the same SampleData.
    //    For each new node, mark is_child = true.
    Node* shared_head = NULL;
    Node* shared_tail = NULL;
    Node* temp = src_curr;
    for (size_t i = 0; i < len && temp != NULL; i++) {
        // Increment ref_count since this sample is now shared
        temp->sample_ptr->ref_count++;

        // Create new node and mark it as a child.
        Node* new_node = (Node*)malloc(sizeof(Node));
        new_node->sample_ptr = temp->sample_ptr;
        new_node->next = NULL;
        new_node->is_child = true;  // <<–– This line is the key change

        if (shared_head == NULL) {
            shared_head = new_node;
        } else {
            shared_tail->next = new_node;
        }
        shared_tail = new_node;

        temp = temp->next;
    }

    // 3. Insert the chain into dest_track at position destpos.
    Node* prev = NULL;
    Node* dest_curr = dest_track->head;
    for (size_t i = 0; i < destpos && dest_curr != NULL; i++) {
        prev = dest_curr;
        dest_curr = dest_curr->next;
    }
    if (prev == NULL) {
        shared_tail->next = dest_track->head;
        dest_track->head = shared_head;
    } else {
        prev->next = shared_head;
        shared_tail->next = dest_curr;
    }

    // 4. Update the destination track's length
    dest_track->length += len;
}



/* reference:
(1) https://stackoverflow.com/questions/33663296/how-to-copy-a-buffer-to-a-character-pointer-in-c (copy to buffer in c)
(2) https://www.tutorialspoint.com/c_standard_library/c_function_memcpy.htm (memcpy args)
*/