#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <set>

using namespace std;

const int MAX_KEY_SIZE = 65;  // 64 bytes + 1 for null terminator
const int ORDER = 100;  // B+ tree order, tuned for performance
const int MAX_KEYS = ORDER - 1;
const int MIN_KEYS = ORDER / 2;

// Structure for a key-value pair
struct Entry {
    char key[MAX_KEY_SIZE];
    int value;

    Entry() {
        memset(key, 0, MAX_KEY_SIZE);
        value = 0;
    }

    Entry(const char* k, int v) : value(v) {
        strncpy(key, k, MAX_KEY_SIZE - 1);
        key[MAX_KEY_SIZE - 1] = '\0';
    }

    bool operator<(const Entry& other) const {
        int cmp = strcmp(key, other.key);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }

    bool operator==(const Entry& other) const {
        return strcmp(key, other.key) == 0 && value == other.value;
    }
};

// Node structure for B+ Tree
struct Node {
    bool isLeaf;
    int numKeys;
    Entry entries[MAX_KEYS];
    int children[ORDER];  // For internal nodes: child file positions
    int next;  // For leaf nodes: next leaf position

    Node() : isLeaf(true), numKeys(0), next(-1) {
        memset(children, -1, sizeof(children));
    }
};

class BPlusTree {
private:
    fstream file;
    int rootPos;
    int nextPos;

    // Read node from file
    Node readNode(int pos) {
        Node node;
        if (pos < 0) return node;

        file.seekg(pos);
        file.read((char*)&node, sizeof(Node));
        return node;
    }

    // Write node to file
    int writeNode(const Node& node, int pos = -1) {
        if (pos < 0) {
            pos = nextPos;
            nextPos += sizeof(Node);
        }

        file.seekp(pos);
        file.write((const char*)&node, sizeof(Node));
        file.flush();
        return pos;
    }

    // Update metadata (root position and next position)
    void updateMeta() {
        file.seekp(0);
        file.write((const char*)&rootPos, sizeof(int));
        file.write((const char*)&nextPos, sizeof(int));
        file.flush();
    }

    // Find the appropriate child index for a given entry
    int findChildIndex(const Node& node, const Entry& entry) {
        int i;
        for (i = 0; i < node.numKeys; i++) {
            if (entry < node.entries[i]) {
                break;
            }
        }
        return i;
    }

    // Split a leaf node
    int splitLeaf(int pos, Node& node, Entry& upEntry) {
        Node newNode;
        newNode.isLeaf = true;
        newNode.next = node.next;

        int mid = (MAX_KEYS + 1) / 2;
        newNode.numKeys = node.numKeys - mid;

        for (int i = 0; i < newNode.numKeys; i++) {
            newNode.entries[i] = node.entries[mid + i];
        }

        node.numKeys = mid;
        node.next = nextPos;

        writeNode(node, pos);
        int newPos = writeNode(newNode);

        upEntry = newNode.entries[0];
        return newPos;
    }

    // Split an internal node
    int splitInternal(int pos, Node& node, Entry& upEntry) {
        Node newNode;
        newNode.isLeaf = false;

        int mid = MAX_KEYS / 2;
        newNode.numKeys = node.numKeys - mid - 1;

        for (int i = 0; i < newNode.numKeys; i++) {
            newNode.entries[i] = node.entries[mid + 1 + i];
            newNode.children[i] = node.children[mid + 1 + i];
        }
        newNode.children[newNode.numKeys] = node.children[node.numKeys];

        upEntry = node.entries[mid];
        node.numKeys = mid;

        writeNode(node, pos);
        int newPos = writeNode(newNode);

        return newPos;
    }

    // Insert into node (recursive)
    bool insertIntoNode(int pos, const Entry& entry, Entry& upEntry, int& newChildPos) {
        Node node = readNode(pos);

        if (node.isLeaf) {
            // Check if entry already exists
            for (int i = 0; i < node.numKeys; i++) {
                if (node.entries[i] == entry) {
                    return false;  // Duplicate
                }
            }

            // Find insertion position
            int i = node.numKeys - 1;
            while (i >= 0 && entry < node.entries[i]) {
                node.entries[i + 1] = node.entries[i];
                i--;
            }
            node.entries[i + 1] = entry;
            node.numKeys++;

            if (node.numKeys <= MAX_KEYS) {
                writeNode(node, pos);
                return false;  // No split needed
            }

            // Split leaf
            newChildPos = splitLeaf(pos, node, upEntry);
            return true;
        } else {
            // Internal node
            int childIdx = findChildIndex(node, entry);
            Entry childUpEntry;
            int childNewPos;

            if (!insertIntoNode(node.children[childIdx], entry, childUpEntry, childNewPos)) {
                return false;  // No split in child
            }

            // Insert the split key from child
            int i = node.numKeys - 1;
            while (i >= childIdx && childUpEntry < node.entries[i]) {
                node.entries[i + 1] = node.entries[i];
                node.children[i + 2] = node.children[i + 1];
                i--;
            }
            node.entries[i + 1] = childUpEntry;
            node.children[i + 2] = childNewPos;
            node.numKeys++;

            if (node.numKeys <= MAX_KEYS) {
                writeNode(node, pos);
                return false;
            }

            // Split internal node
            newChildPos = splitInternal(pos, node, upEntry);
            return true;
        }
    }

    // Delete from node (recursive)
    bool deleteFromNode(int pos, const Entry& entry) {
        Node node = readNode(pos);

        if (node.isLeaf) {
            int idx = -1;
            for (int i = 0; i < node.numKeys; i++) {
                if (node.entries[i] == entry) {
                    idx = i;
                    break;
                }
            }

            if (idx == -1) {
                return false;  // Not found
            }

            for (int i = idx; i < node.numKeys - 1; i++) {
                node.entries[i] = node.entries[i + 1];
            }
            node.numKeys--;

            writeNode(node, pos);
            return true;
        } else {
            int childIdx = findChildIndex(node, entry);
            return deleteFromNode(node.children[childIdx], entry);
        }
    }

    // Find all values for a given key
    void findInNode(int pos, const char* key, vector<int>& result) {
        if (pos < 0) return;

        Node node = readNode(pos);

        if (node.isLeaf) {
            for (int i = 0; i < node.numKeys; i++) {
                if (strcmp(node.entries[i].key, key) == 0) {
                    result.push_back(node.entries[i].value);
                }
            }
            // Check next leaf
            if (node.next >= 0) {
                Node nextNode = readNode(node.next);
                for (int i = 0; i < nextNode.numKeys; i++) {
                    if (strcmp(nextNode.entries[i].key, key) == 0) {
                        result.push_back(nextNode.entries[i].value);
                    } else if (strcmp(nextNode.entries[i].key, key) > 0) {
                        break;
                    }
                }
            }
        } else {
            // Find the appropriate child
            int childIdx = 0;
            for (int i = 0; i < node.numKeys; i++) {
                if (strcmp(key, node.entries[i].key) < 0) {
                    break;
                }
                childIdx = i + 1;
            }
            findInNode(node.children[childIdx], key, result);
        }
    }

public:
    BPlusTree(const string& filename) {
        bool isNew = false;

        // Try to open existing file
        file.open(filename, ios::in | ios::out | ios::binary);

        if (!file.is_open()) {
            // Create new file
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            isNew = true;
        }

        if (isNew) {
            // Initialize new tree
            rootPos = 2 * sizeof(int);
            nextPos = rootPos + sizeof(Node);

            Node root;
            root.isLeaf = true;
            root.numKeys = 0;

            writeNode(root, rootPos);
            updateMeta();
        } else {
            // Read metadata
            file.seekg(0);
            file.read((char*)&rootPos, sizeof(int));
            file.read((char*)&nextPos, sizeof(int));
        }
    }

    ~BPlusTree() {
        if (file.is_open()) {
            file.close();
        }
    }

    void insert(const char* key, int value) {
        Entry entry(key, value);
        Entry upEntry;
        int newChildPos;

        if (insertIntoNode(rootPos, entry, upEntry, newChildPos)) {
            // Root split
            Node newRoot;
            newRoot.isLeaf = false;
            newRoot.numKeys = 1;
            newRoot.entries[0] = upEntry;
            newRoot.children[0] = rootPos;
            newRoot.children[1] = newChildPos;

            rootPos = writeNode(newRoot);
            updateMeta();
        }
    }

    void remove(const char* key, int value) {
        Entry entry(key, value);
        deleteFromNode(rootPos, entry);
    }

    void find(const char* key) {
        vector<int> result;
        findInNode(rootPos, key, result);

        if (result.empty()) {
            cout << "null" << endl;
        } else {
            sort(result.begin(), result.end());
            for (size_t i = 0; i < result.size(); i++) {
                if (i > 0) cout << " ";
                cout << result[i];
            }
            cout << endl;
        }
    }
};

int main() {
    BPlusTree tree("data.db");

    int n;
    cin >> n;

    string cmd;
    for (int i = 0; i < n; i++) {
        cin >> cmd;

        if (cmd == "insert") {
            char key[MAX_KEY_SIZE];
            int value;
            cin >> key >> value;
            tree.insert(key, value);
        } else if (cmd == "delete") {
            char key[MAX_KEY_SIZE];
            int value;
            cin >> key >> value;
            tree.remove(key, value);
        } else if (cmd == "find") {
            char key[MAX_KEY_SIZE];
            cin >> key;
            tree.find(key);
        }
    }

    return 0;
}
