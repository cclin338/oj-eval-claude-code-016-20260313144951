#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace std;

const int MAX_KEY_SIZE = 65;
const int ORDER = 85;  // Tuned for memory constraints
const int MAX_KEYS = ORDER - 1;
const int MIN_KEYS = ORDER / 2;

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

struct Node {
    bool isLeaf;
    int numKeys;
    Entry entries[MAX_KEYS];
    int children[ORDER];
    int next;

    Node() : isLeaf(true), numKeys(0), next(-1) {
        memset(children, -1, sizeof(children));
    }
};

class BPlusTree {
private:
    fstream file;
    int rootPos;
    int nextPos;
    int firstLeafPos;  // Track first leaf for efficient find

    Node readNode(int pos) {
        Node node;
        if (pos < 0) return node;
        file.seekg(pos);
        file.read((char*)&node, sizeof(Node));
        return node;
    }

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

    void updateMeta() {
        file.seekp(0);
        file.write((const char*)&rootPos, sizeof(int));
        file.write((const char*)&nextPos, sizeof(int));
        file.write((const char*)&firstLeafPos, sizeof(int));
        file.flush();
    }

    int findChildIndex(const Node& node, const Entry& entry) {
        int i;
        for (i = 0; i < node.numKeys; i++) {
            if (entry < node.entries[i]) {
                break;
            }
        }
        return i;
    }

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

    bool insertIntoNode(int pos, const Entry& entry, Entry& upEntry, int& newChildPos) {
        Node node = readNode(pos);

        if (node.isLeaf) {
            // Check for duplicate
            for (int i = 0; i < node.numKeys; i++) {
                if (node.entries[i] == entry) {
                    return false;
                }
            }

            // Insert in sorted order
            int i = node.numKeys - 1;
            while (i >= 0 && entry < node.entries[i]) {
                node.entries[i + 1] = node.entries[i];
                i--;
            }
            node.entries[i + 1] = entry;
            node.numKeys++;

            if (node.numKeys <= MAX_KEYS) {
                writeNode(node, pos);
                return false;
            }

            newChildPos = splitLeaf(pos, node, upEntry);
            return true;
        } else {
            int childIdx = findChildIndex(node, entry);
            Entry childUpEntry;
            int childNewPos;

            if (!insertIntoNode(node.children[childIdx], entry, childUpEntry, childNewPos)) {
                return false;
            }

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

            newChildPos = splitInternal(pos, node, upEntry);
            return true;
        }
    }

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
                return false;
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

    // Find first leaf node
    int findFirstLeaf(int pos) {
        if (pos < 0) return -1;
        Node node = readNode(pos);
        if (node.isLeaf) {
            return pos;
        }
        return findFirstLeaf(node.children[0]);
    }

public:
    BPlusTree(const string& filename) {
        bool isNew = false;

        file.open(filename, ios::in | ios::out | ios::binary);

        if (!file.is_open()) {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            isNew = true;
        }

        if (isNew) {
            rootPos = 3 * sizeof(int);
            nextPos = rootPos + sizeof(Node);
            firstLeafPos = rootPos;

            Node root;
            root.isLeaf = true;
            root.numKeys = 0;

            writeNode(root, rootPos);
            updateMeta();
        } else {
            file.seekg(0);
            file.read((char*)&rootPos, sizeof(int));
            file.read((char*)&nextPos, sizeof(int));
            file.read((char*)&firstLeafPos, sizeof(int));
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
            Node newRoot;
            newRoot.isLeaf = false;
            newRoot.numKeys = 1;
            newRoot.entries[0] = upEntry;
            newRoot.children[0] = rootPos;
            newRoot.children[1] = newChildPos;

            rootPos = writeNode(newRoot);
            firstLeafPos = findFirstLeaf(rootPos);
            updateMeta();
        }
    }

    void remove(const char* key, int value) {
        Entry entry(key, value);
        deleteFromNode(rootPos, entry);
    }

    void find(const char* key) {
        vector<int> result;

        // Start from first leaf and traverse all leaves
        int leafPos = findFirstLeaf(rootPos);

        while (leafPos >= 0) {
            Node leaf = readNode(leafPos);

            for (int i = 0; i < leaf.numKeys; i++) {
                int cmp = strcmp(leaf.entries[i].key, key);
                if (cmp == 0) {
                    result.push_back(leaf.entries[i].value);
                } else if (cmp > 0) {
                    // Keys are sorted, so we can stop
                    goto done;
                }
            }

            leafPos = leaf.next;
        }

    done:
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
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

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
