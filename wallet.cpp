#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QMessageBox>

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

#include "blockchain_core.cpp"

static std::string pubKeyHashFromECKey(EC_KEY *ecKey) {
    // Get public key in compressed form
    int size = i2o_ECPublicKey(ecKey, NULL);
    unsigned char *buffer = new unsigned char[size];
    unsigned char *p = buffer;
    i2o_ECPublicKey(ecKey, &p);

    // Use SHA-256 to create a "pubKeyHash" (this is simplified)
    std::string rawPubKey((char*)buffer, size);
    std::string hash = sha256(rawPubKey);

    delete[] buffer;
    return hash;
}

// Minimal wallet main window
class WalletWindow : public QMainWindow {
    Q_OBJECT

public:
    WalletWindow(QWidget *parent=nullptr) : QMainWindow(parent) {
        QWidget *central = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(central);

        // Generate new address button
        QPushButton *genBtn = new QPushButton("Generate New Address", this);
        layout->addWidget(genBtn);
        connect(genBtn, &QPushButton::clicked, this, &WalletWindow::onGenerateAddress);

        // Display addresses
        addressDisplay = new QTextEdit(this);
        addressDisplay->setReadOnly(true);
        layout->addWidget(addressDisplay);

        // Send transaction fields
        QLabel *destLabel = new QLabel("Destination PubKeyHash:");
        layout->addWidget(destLabel);
        destEdit = new QLineEdit(this);
        layout->addWidget(destEdit);

        QLabel *amtLabel = new QLabel("Amount (in satoshis):");
        layout->addWidget(amtLabel);
        amtEdit = new QLineEdit(this);
        layout->addWidget(amtEdit);

        QPushButton *sendBtn = new QPushButton("Send Transaction", this);
        layout->addWidget(sendBtn);
        connect(sendBtn, &QPushButton::clicked, this, &WalletWindow::onSendTransaction);

        // Show UTXO balance
        QPushButton *balBtn = new QPushButton("Show Balance", this);
        layout->addWidget(balBtn);
        connect(balBtn, &QPushButton::clicked, this, &WalletWindow::onShowBalance);

        setCentralWidget(central);
    }

private slots:
    void onGenerateAddress() {
        // Generate a new ECDSA secp256k1 key
        EC_KEY *ecKey = EC_KEY_new_by_curve_name(NID_secp256k1);
        if (!ecKey) {
            QMessageBox::warning(this, "Error", "Failed to create key object.");
            return;
        }
        if (!EC_KEY_generate_key(ecKey)) {
            QMessageBox::warning(this, "Error", "Failed to generate EC key.");
            EC_KEY_free(ecKey);
            return;
        }
        std::string pkHash = pubKeyHashFromECKey(ecKey);

        // In real code, store the private key (encrypted) in a secure location
        // For demo, we show in console
        const BIGNUM *privBN = EC_KEY_get0_private_key(ecKey);
        char *hex = BN_bn2hex(privBN);
        std::string privKeyHex(hex);
        OPENSSL_free(hex);

        // Display
        std::stringstream ss;
        ss << "PrivKey: " << privKeyHex << "\nPubKeyHash: " << pkHash << "\n\n";
        addressDisplay->insertPlainText(QString::fromStdString(ss.str()));

        // We could store this in an internal list
        knownKeys[pkHash] = privKeyHex;

        EC_KEY_free(ecKey);
    }

    void onSendTransaction() {
        // Build a transaction from the first known address
        if (knownKeys.empty()) {
            QMessageBox::information(this, "No Keys", "Generate an address first.");
            return;
        }
        std::string fromPubKeyHash = knownKeys.begin()->first; // pick first for simplicity
        std::string privKeyHex = knownKeys.begin()->second;    // not used in detail here

        std::string toPubKeyHash = destEdit->text().toStdString();
        uint64_t amt = static_cast<uint64_t>(amtEdit->text().toLongLong());

        // We would search our UTXOs for fromPubKeyHash, sum them up, create inputs, sign them, etc.
        // For brevity, let's do a simplified single input transaction

        // This is purely conceptual: you'd need to find real UTXOs matching `fromPubKeyHash`.
        // We'll just pick the first matching UTXO from the global set if available.

        std::string foundKey;
        uint64_t foundAmount = 0;
        for (auto &kv : g_utxoSet) {
            if (kv.second.pubKeyHash == fromPubKeyHash) {
                foundKey = kv.first;
                foundAmount = kv.second.amount;
                break;
            }
        }
        if (foundKey.empty()) {
            QMessageBox::warning(this, "Error", "No UTXOs found for your address. No balance?");
            return;
        }
        // Build the transaction
        Transaction tx;
        tx.version = 1;
        tx.lockTime = 0;
        TxInput in;
        // parse foundKey -> txid:index
        auto pos = foundKey.find(':');
        in.txid = foundKey.substr(0, pos);
        in.index = std::stoi(foundKey.substr(pos+1));
        in.signature = "dummy-signature"; // In real code, sign with ECDSA
        tx.inputs.push_back(in);

        // Output to destination
        TxOutput out;
        out.amount = amt;
        out.pubKeyHash = toPubKeyHash;
        tx.outputs.push_back(out);

        // Possibly add change output
        uint64_t fee = 1000; // Hard-coded minimal fee
        uint64_t change = foundAmount - amt - fee;
        if (change > 0) {
            TxOutput changeOut;
            changeOut.amount = change;
            changeOut.pubKeyHash = fromPubKeyHash;
            tx.outputs.push_back(changeOut);
        }

        // Now we can attempt to validate and apply the transaction to the local node
        Blockchain *chain = getBlockchain();
        if (!chain->validateTransaction(tx)) {
            QMessageBox::warning(this, "Error", "Transaction invalid or insufficient funds.");
            return;
        }
        chain->applyTransaction(tx);

        // In a real system, we would broadcast this transaction over the P2P network.

        QMessageBox::information(this, "Success", "Transaction created and applied locally!");
    }

    void onShowBalance() {
        // Summation of all UTXOs that match our known addresses
        uint64_t balance = 0;
        for (auto &kv : g_utxoSet) {
            UTXO utxo = kv.second;
            if (knownKeys.find(utxo.pubKeyHash) != knownKeys.end()) {
                balance += utxo.amount;
            }
        }
        std::stringstream ss;
        ss << "Total balance for your addresses: " << balance << " satoshis";
        QMessageBox::information(this, "Balance", QString::fromStdString(ss.str()));
    }

private:
    QTextEdit *addressDisplay;
    QLineEdit *destEdit;
    QLineEdit *amtEdit;

    // Maps pubKeyHash -> privateKeyHex
    std::map<std::string, std::string> knownKeys;
};

#include <QMetaType>
Q_DECLARE_METATYPE(std::string)

// The wallet's main entry point
int main_wallet(int argc, char *argv[]) {
    QApplication app(argc, argv);

    initBlockchain(); // Initialize our blockchain instance

    WalletWindow window;
    window.setWindowTitle("MyCoin Wallet");
    window.resize(400, 300);
    window.show();

    return app.exec();
}

#include "wallet.moc"
